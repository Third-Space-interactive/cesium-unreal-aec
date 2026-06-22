// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionManager.h"
#include "Cesium3DTileset.h"
#include "CesiumGltfPointsComponent.h"
#include "CesiumPointCloudCollisionProxy.h"

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
    CollisionProxy->UpdateBoundsFromTileset();
    bBoundsNeedUpdate = false;
    SetComponentTickEnabled(false);
  }
}

void UCesiumPointCloudCollisionManager::OnTileMeshPrimitiveLoaded(
    ICesiumLoadedTilePrimitive& TilePrimitive) {
  UStaticMeshComponent& MeshComp = TilePrimitive.GetMeshComponent();
  if (Cast<UCesiumGltfPointsComponent>(&MeshComp)) {
    bHasPointCloudTiles = true;
  }
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
  bHasPointCloudTiles = false;
  bBoundsNeedUpdate = false;
  SetComponentTickEnabled(false);
}
