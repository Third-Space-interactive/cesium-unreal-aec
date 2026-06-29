// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionManager.h"
#include "Cesium3DTileset.h"
#include "CesiumGltfPointsComponent.h"
#include "CesiumLoadedTile.h"
#include "CesiumPointCloudBounds.h"
#include "CesiumPointCloudCollisionProxy.h"
#include "CesiumPrimitive.h"
#include "CesiumTilePointIndex.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "CesiumGltf/Accessor.h"
#include "CesiumGltf/AccessorView.h"
#include "CesiumGltf/Model.h"
#include "CesiumGltf/VertexAttributeSemantics.h"

UCesiumPointCloudCollisionManager::FResidentPointsTile::FResidentPointsTile() =
    default;
UCesiumPointCloudCollisionManager::FResidentPointsTile::~FResidentPointsTile() =
    default;
UCesiumPointCloudCollisionManager::FResidentPointsTile::FResidentPointsTile(
    FResidentPointsTile&&) = default;
UCesiumPointCloudCollisionManager::FResidentPointsTile&
UCesiumPointCloudCollisionManager::FResidentPointsTile::operator=(
    FResidentPointsTile&&) = default;

UCesiumPointCloudCollisionManager::UCesiumPointCloudCollisionManager() {
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;
  PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCesiumPointCloudCollisionManager::BeginPlay() {
  Super::BeginPlay();

  ACesium3DTileset* Tileset = Cast<ACesium3DTileset>(GetOwner());
  if (Tileset) {
    Tileset->SetLifecycleEventReceiver(this);
  }
}

void UCesiumPointCloudCollisionManager::EndPlay(
    const EEndPlayReason::Type EndPlayReason) {
  DestroyCollisionProxy();

  ACesium3DTileset* Tileset = Cast<ACesium3DTileset>(GetOwner());
  if (Tileset) {
    // Only clear the receiver if we're still the active one
    if (Tileset->GetLifecycleEventReceiver() ==
        static_cast<ICesium3DTilesetLifecycleEventReceiver*>(this)) {
      Tileset->SetLifecycleEventReceiver(nullptr);
    }
  }

  Super::EndPlay(EndPlayReason);
}

void UCesiumPointCloudCollisionManager::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (bBoundsNeedUpdate && CollisionProxy) {
    RecomputeTierOneBounds();
    bBoundsNeedUpdate = false;
    SetComponentTickEnabled(false);
  }
}

void UCesiumPointCloudCollisionManager::OnTileMeshPrimitiveLoaded(
    ICesiumLoadedTilePrimitive& TilePrimitive) {
  UStaticMeshComponent& MeshComp = TilePrimitive.GetMeshComponent();
  if (!Cast<UCesiumGltfPointsComponent>(&MeshComp)) {
    return;
  }
  bHasPointCloudTiles = true;

  if (!bEnablePreciseRefine) {
    return;
  }

  TArray<FVector3f> LocalPoints =
      ReadDownsampledLocalPoints(TilePrimitive, MaxCapturePoints);
  if (LocalPoints.Num() == 0) {
    return;
  }

  ICesiumLoadedTile& LoadedTile = TilePrimitive.GetLoadedTile();
  FResidentPointsTile Tile;
  Tile.Component = &MeshComp;
  Tile.TrimmedLocalBox =
      CesiumPointCloudBounds::ComputeTrimmedLocalBox(LocalPoints, BoxClipFraction);
  Tile.LocalPoints = MoveTemp(LocalPoints);
  Tile.OwningTile = &LoadedTile;
  Tile.GltfToUnrealScale =
      LoadedTile.GetGltfToUnrealLocalVertexPositionScaleFactor();
  ResidentTiles.Add(
      TObjectKey<UPrimitiveComponent>(&MeshComp),
      MoveTemp(Tile));
}

void UCesiumPointCloudCollisionManager::OnTileLoaded(
    ICesiumLoadedTile& Tile) {
  if (!bHasPointCloudTiles || !bAutoCreateProxy) {
    return;
  }

  if (!CollisionProxy) {
    CreateCollisionProxy();
  }

  // Defer bounds update to TickComponent to batch multiple tile loads
  bBoundsNeedUpdate = true;
  SetComponentTickEnabled(true);
}

void UCesiumPointCloudCollisionManager::OnTileVisibilityChanged(
    ICesiumLoadedTile& Tile,
    bool bVisible) {
  if (CollisionProxy) {
    bBoundsNeedUpdate = true;
    SetComponentTickEnabled(true);
  }
}

void UCesiumPointCloudCollisionManager::OnTileUnloading(
    ICesiumLoadedTile& Tile) {
  for (auto It = ResidentTiles.CreateIterator(); It; ++It) {
    if (!It->Value.Component.IsValid() || It->Value.OwningTile == &Tile) {
      It.RemoveCurrent();
    }
  }
  if (CollisionProxy) {
    bBoundsNeedUpdate = true;
    SetComponentTickEnabled(true);
  }
}

UCesiumPointCloudCollisionProxy*
UCesiumPointCloudCollisionManager::GetCollisionProxy() const {
  return CollisionProxy;
}

void UCesiumPointCloudCollisionManager::CreateCollisionProxy() {
  if (CollisionProxy) {
    return;
  }

  AActor* Owner = GetOwner();
  if (!Owner) {
    return;
  }

  CollisionProxy = NewObject<UCesiumPointCloudCollisionProxy>(
      Owner,
      UCesiumPointCloudCollisionProxy::StaticClass(),
      TEXT("PointCloudCollisionProxy"));

  CollisionProxy->ConfigureCollision(TraceChannel);
  CollisionProxy->SetupAttachment(Owner->GetRootComponent());
  CollisionProxy->RegisterComponent();
  CollisionProxy->UpdateBoundsFromTileset();
}

void UCesiumPointCloudCollisionManager::DestroyCollisionProxy() {
  if (CollisionProxy) {
    CollisionProxy->DestroyComponent();
    CollisionProxy = nullptr;
  }
  ResidentTiles.Empty();
  bHasPointCloudTiles = false;
  bBoundsNeedUpdate = false;
  SetComponentTickEnabled(false);
}

static FCesiumTilePointIndexParams
MakeIndexParams(const UCesiumPointCloudCollisionManager& Mgr) {
  FCesiumTilePointIndexParams P;
  P.MaxGridDim = Mgr.MaxGridDim;
  P.MinVoxelWorldSize = Mgr.MinVoxelWorldSize;
  P.MaxVoxelWorldSize = Mgr.MaxVoxelWorldSize;
  P.RayPickRadiusFactor = Mgr.RayPickRadiusFactor;
  return P;
}

TArray<FVector3f>
UCesiumPointCloudCollisionManager::ReadDownsampledLocalPoints(
    ICesiumLoadedTilePrimitive& TilePrimitive,
    int32 MaxPoints) {
  TArray<FVector3f> Out;
  ICesiumLoadedTile& Tile = TilePrimitive.GetLoadedTile();
  const CesiumGltf::Model* Model = Tile.GetGltfModel();
  const CesiumGltf::MeshPrimitive* Prim = TilePrimitive.GetMeshPrimitive();
  if (!Model || !Prim) {
    return Out;
  }

  auto PosIt =
      Prim->attributes.find(CesiumGltf::VertexAttributeSemantics::POSITION);
  if (PosIt == Prim->attributes.end()) {
    return Out;
  }

  // Use the index-based AccessorView constructor (validates bounds internally).
  static_assert(
      sizeof(FVector3f) == 12,
      "AccessorView<FVector3f> assumes tightly-packed 12-byte positions");
  CesiumGltf::AccessorView<FVector3f> View(*Model, PosIt->second);
  if (View.status() != CesiumGltf::AccessorViewStatus::Valid ||
      View.size() == 0) {
    return Out;
  }

  // Component-wise scale from glTF accessor values to Unreal local positions.
  const FVector Scale = Tile.GetGltfToUnrealLocalVertexPositionScaleFactor();
  const int64_t Count = View.size();
  const int32 Stride =
      FMath::Max(1, FMath::CeilToInt((float)Count / FMath::Max(MaxPoints, 1)));
  Out.Reserve((int32)(Count / Stride) + 1);
  for (int64_t i = 0; i < Count; i += Stride) {
    const FVector3f P = View[(int32)i];
    Out.Add(FVector3f(
        P.X * (float)Scale.X,
        P.Y * (float)Scale.Y,
        P.Z * (float)Scale.Z));
  }
  return Out;
}

TArray<FVector3f>
UCesiumPointCloudCollisionManager::ReadFullResPointsInWindow(
    UPrimitiveComponent* Component,
    const FVector& Scale,
    const FVector& Center,
    double Radius) {
  TArray<FVector3f> Out;
  ICesiumPrimitive* Primitive = Cast<ICesiumPrimitive>(Component);
  if (!Primitive) {
    return Out;
  }

  const CesiumGltf::AccessorView<FVector3f>& View =
      Primitive->getPrimitiveData().positionAccessor;
  if (View.status() != CesiumGltf::AccessorViewStatus::Valid ||
      View.size() == 0) {
    return Out;
  }

  // Collect only the full-res points inside the snap window, so memory stays
  // bounded (no full-res copy retained) while accuracy uses the real points.
  const FVector3f CenterF((float)Center.X, (float)Center.Y, (float)Center.Z);
  const float RadiusSq = (float)(Radius * Radius);
  const int64_t Count = View.size();
  for (int64_t i = 0; i < Count; ++i) {
    const FVector3f Raw = View[(int32)i];
    const FVector3f P(
        Raw.X * (float)Scale.X,
        Raw.Y * (float)Scale.Y,
        Raw.Z * (float)Scale.Z);
    if (FVector3f::DistSquared(P, CenterF) <= RadiusSq) {
      Out.Add(P);
    }
  }
  return Out;
}

namespace {
// One per-tile hit candidate, collected in pass 1 of RefineRay.
struct FRefineCandidate {
  FVector WorldPoint = FVector::ZeroVector;
  FVector WorldNormal = FVector::ZeroVector;
  double Along = 0.0;          // depth along the ray from the camera
  double Perp = 0.0;           // perpendicular distance to the ray
  // Debug-draw payload for the winning candidate.
  const FCesiumTilePointIndex* Index = nullptr;
  FTransform Xf;
  FVector CentroidLocal = FVector::ZeroVector;     // post-snap (green)
  FVector RawCentroidLocal = FVector::ZeroVector;  // pre-snap centroid (red)
  // Hit slid onto the ray along the local surface plane; applied only if this
  // candidate wins. Selection uses WorldPoint's true perp/along, not this.
  // Equals WorldPoint when projection is disabled.
  FVector ProjectedWorldPoint = FVector::ZeroVector;
};
} // namespace

FCesiumPointCloudHit UCesiumPointCloudCollisionManager::RefineRay(
    const FVector& WorldOrigin,
    const FVector& WorldDir,
    double MaxDistance) {
  FCesiumPointCloudHit Best;
  ACesium3DTileset* Tileset = Cast<ACesium3DTileset>(GetOwner());

  const FVector WDir = WorldDir.GetSafeNormal();

  // Pass 1: collect one candidate hit per resident tile.
  TArray<FRefineCandidate> Candidates;
  Candidates.Reserve(ResidentTiles.Num());

  for (auto It = ResidentTiles.CreateIterator(); It; ++It) {
    UPrimitiveComponent* Comp = It->Value.Component.Get();
    if (!IsValid(Comp)) {
      It.RemoveCurrent();
      continue;
    }

    // World ray -> tile local space using two-point method (handles scale).
    const FTransform Xf = Comp->GetComponentTransform();
    const FVector LO = Xf.InverseTransformPosition(WorldOrigin);
    const FVector L1 =
        Xf.InverseTransformPosition(WorldOrigin + WDir * MaxDistance);
    const FVector LDir = (L1 - LO).GetSafeNormal();
    if (LDir.IsNearlyZero()) {
      continue;
    }

    // Broad reject against the cached local box.
    if (It->Value.TrimmedLocalBox.IsValid) {
      if (!FMath::LineBoxIntersection(
              It->Value.TrimmedLocalBox, LO, L1, L1 - LO)) {
        continue;
      }
    }

    // Lazily build the index.
    if (!It->Value.Index && It->Value.LocalPoints.Num() > 0) {
      It->Value.Index = FCesiumTilePointIndex::BuildFromLocalPoints(
          It->Value.LocalPoints, MakeIndexParams(*this));
    }
    if (!It->Value.Index) {
      continue;
    }

    // Local-space ray length (differs from MaxDistance under non-unit scale).
    const double LocalMax = (L1 - LO).Size();
    FCesiumTileRayHit LHit = It->Value.Index->RayMarch(LO, LDir, LocalMax);
    if (!LHit.bHit) {
      continue;
    }
    const FVector RawCentroidLocal = LHit.LocalPoint; // pre-snap (debug trace)

    // Snap against full-resolution points read straight from the glTF accessor
    // (only those within the snap window around the coarse hit), so accuracy is
    // not capped by the decimated capture set. Fall back to the captured points
    // when the full-res accessor is unavailable (e.g. test components).
    const double SnapWindow =
        RayPickRadiusFactor * It->Value.Index->GetVoxelEdge() * 2.0;
    const TArray<FVector3f> FullResPoints = ReadFullResPointsInWindow(
        Comp, It->Value.GltfToUnrealScale, LHit.LocalPoint, SnapWindow);
    const TArray<FVector3f>& SnapPoints =
        FullResPoints.Num() > 0 ? FullResPoints : It->Value.LocalPoints;
    LHit = It->Value.Index->RefineHitToNearestPoint(LO, LDir, LHit, SnapPoints);

    const FVector WPoint = Xf.TransformPosition(LHit.LocalPoint);
    const double Along = FVector::DotProduct(WPoint - WorldOrigin, WDir);
    if (Along < 0.0 || Along > MaxDistance) {
      continue;
    }
    const FVector ClosestOnRay = WorldOrigin + WDir * Along;
    const double Perp = FVector::Dist(ClosestOnRay, WPoint);

    // Slide the hit onto the ray along its local surface plane here, where the
    // local ray is in scope, but apply it only if this candidate wins below -
    // selection uses WPoint's true perp/along, so projecting now (which would
    // zero every candidate's perp) would not corrupt the cross-tile tie-break.
    FVector ProjectedWorldPoint = WPoint;
    if (bProjectHitToSurface) {
      const FCesiumTileRayHit P =
          FCesiumTilePointIndex::ProjectHitOntoRay(LO, LDir, LHit);
      ProjectedWorldPoint = Xf.TransformPosition(P.LocalPoint);
    }

    FRefineCandidate C;
    C.WorldPoint = WPoint;
    C.WorldNormal =
        Xf.TransformVectorNoScale(LHit.LocalNormal).GetSafeNormal();
    C.Along = Along;
    C.Perp = Perp;
    C.Index = It->Value.Index.Get();
    C.Xf = Xf;
    C.CentroidLocal = LHit.LocalPoint;
    C.RawCentroidLocal = RawCentroidLocal;
    C.ProjectedWorldPoint = ProjectedWorldPoint;
    Candidates.Add(MoveTemp(C));
  }

  if (Candidates.Num() == 0) {
    return Best;
  }

  // Pass 2: pick the candidate closest to the ray (smallest perp = the surface
  // the cursor is actually on). Among candidates equally on-cursor (perp within
  // OnCursorPerpFactor x the best), the frontmost wins, so a genuine nearer
  // surface beats a farther one and coarse off-axis grabs (high perp) lose.
  double MinPerp = Candidates[0].Perp;
  for (const FRefineCandidate& C : Candidates) {
    MinPerp = FMath::Min(MinPerp, C.Perp);
  }
  const double PerpThreshold = MinPerp * (double)OnCursorPerpFactor;

  const FRefineCandidate* Winner = nullptr;
  for (const FRefineCandidate& C : Candidates) {
    if (C.Perp > PerpThreshold) {
      continue;
    }
    if (!Winner || C.Along < Winner->Along) {
      Winner = &C;
    }
  }
  if (!Winner) {
    return Best;
  }

  Best.bHit = true;
  Best.Tileset = Tileset;
  Best.WorldPoint = Winner->ProjectedWorldPoint;
  Best.WorldNormal = Winner->WorldNormal;
  // True range to the hit (not Along), so the Blueprint library's cross-tileset
  // nearest-wins comparison matches the Phase-1 fallback's Euclidean distance.
  Best.Distance = (float)FVector::Dist(WorldOrigin, Winner->ProjectedWorldPoint);

  if (bDebugDrawVoxels && Winner->Index) {
    DebugDrawTileVoxels(
        *Winner->Index,
        Winner->Xf,
        Winner->RawCentroidLocal,
        Winner->CentroidLocal,
        WorldOrigin,
        WDir);
  }

  return Best;
}

void UCesiumPointCloudCollisionManager::DebugDrawTileVoxels(
    const FCesiumTilePointIndex& Index,
    const FTransform& TileToWorld,
    const FVector& RawCentroidLocal,
    const FVector& SnappedLocal,
    const FVector& WorldOrigin,
    const FVector& WorldDir) const {
#if ENABLE_DRAW_DEBUG
  UWorld* World = GetWorld();
  if (!World) {
    return;
  }

  const double Edge = Index.GetVoxelEdge();
  const FVector Scale = TileToWorld.GetScale3D();
  const FQuat Rot = TileToWorld.GetRotation();
  const FVector BoxExtent = FVector(Edge * 0.5) * Scale;
  const double ScaleAvg = (Scale.X + Scale.Y + Scale.Z) / 3.0;

  // Highlight the voxel the ray-march selected (the raw centroid's cell).
  const FVector HitCellCenterLocal =
      Index.GetCellCenterForLocalPoint(RawCentroidLocal);
  for (const FVector& CenterLocal : Index.GetOccupiedCellCenters()) {
    const bool bIsHitCell = CenterLocal.Equals(HitCellCenterLocal, 0.01);
    DrawDebugBox(
        World,
        TileToWorld.TransformPosition(CenterLocal),
        BoxExtent,
        Rot,
        bIsHitCell ? FColor::Yellow : FColor(70, 70, 70),
        false,
        DebugDrawDuration,
        0,
        bIsHitCell ? 4.f : 0.5f);
  }

  const FVector RawWorld = TileToWorld.TransformPosition(RawCentroidLocal);
  const FVector SnappedWorld = TileToWorld.TransformPosition(SnappedLocal);
  const FVector Dir = WorldDir.GetSafeNormal();
  const float SphereR = (float)(Edge * 0.2 * ScaleAvg);

  // Foot of the perpendicular from each point to the cursor ray. The segment
  // length IS that point's on-screen miss (a point on the ray reprojects onto
  // the cursor, so perpendicular distance == screen offset at that depth).
  const double AlongRaw = FVector::DotProduct(RawWorld - WorldOrigin, Dir);
  const FVector FootRaw = WorldOrigin + Dir * AlongRaw;
  const double AlongSnap = FVector::DotProduct(SnappedWorld - WorldOrigin, Dir);
  const FVector FootSnap = WorldOrigin + Dir * AlongSnap;

  // Cursor ray (cyan), out to just past the hit.
  DrawDebugLine(
      World,
      WorldOrigin,
      WorldOrigin + Dir * (AlongSnap * 1.05),
      FColor::Cyan,
      false,
      DebugDrawDuration,
      0,
      1.f);

  // Raw centroid (red) + its perpendicular miss (orange).
  DrawDebugSphere(
      World, RawWorld, SphereR, 12, FColor::Red, false, DebugDrawDuration, 0, 3.f);
  DrawDebugLine(
      World, RawWorld, FootRaw, FColor::Orange, false, DebugDrawDuration, 0, 2.f);

  // Snapped point (green) + its perpendicular miss (magenta).
  DrawDebugSphere(
      World,
      SnappedWorld,
      SphereR,
      12,
      FColor::Green,
      false,
      DebugDrawDuration,
      0,
      3.f);
  DrawDebugLine(
      World,
      SnappedWorld,
      FootSnap,
      FColor::Magenta,
      false,
      DebugDrawDuration,
      0,
      2.f);
#endif // ENABLE_DRAW_DEBUG
}

void UCesiumPointCloudCollisionManager::AddResidentTileForTesting(
    UPrimitiveComponent* Component,
    const TArray<FVector3f>& LocalPoints) {
  if (!Component) {
    return;
  }
  FResidentPointsTile Tile;
  Tile.Component = Component;
  Tile.LocalPoints = LocalPoints;
  Tile.TrimmedLocalBox =
      CesiumPointCloudBounds::ComputeTrimmedLocalBox(LocalPoints, BoxClipFraction);
  ResidentTiles.Add(TObjectKey<UPrimitiveComponent>(Component), MoveTemp(Tile));
}

void UCesiumPointCloudCollisionManager::RecomputeTierOneBounds() {
  if (!CollisionProxy) {
    return;
  }

  TArray<FBox> WorldBoxes;
  WorldBoxes.Reserve(ResidentTiles.Num());
  for (auto It = ResidentTiles.CreateIterator(); It; ++It) {
    UPrimitiveComponent* Comp = It->Value.Component.Get();
    if (!IsValid(Comp)) {
      It.RemoveCurrent();
      continue;
    }
    if (!It->Value.TrimmedLocalBox.IsValid) {
      continue;
    }
    // Local trimmed box -> world via the tile component's transform.
    WorldBoxes.Add(
        It->Value.TrimmedLocalBox.TransformBy(Comp->GetComponentTransform()));
  }

  if (WorldBoxes.Num() == 0) {
    return;
  }
  const FBox WorldBox =
      CesiumPointCloudBounds::UnionWithMadRejection(WorldBoxes, MadThreshold);
  CollisionProxy->SetWorldBounds(WorldBox);
}
