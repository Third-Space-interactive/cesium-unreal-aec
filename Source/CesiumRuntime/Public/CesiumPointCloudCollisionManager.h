// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Cesium3DTilesetLifecycleEventReceiver.h"
#include "CesiumPointCloudHit.h"
#include "Components/ActorComponent.h"
#include "Templates/UniquePtr.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtr.h"
#include "CesiumPointCloudCollisionManager.generated.h"

class UCesiumPointCloudCollisionProxy;
class ACesium3DTileset;
class UPrimitiveComponent;
class FCesiumTilePointIndex;
class ICesiumLoadedTile;

/**
 * Add this component to an ACesium3DTileset to enable runtime line-trace
 * selection of point clouds. It listens for point cloud tiles loading (via
 * ICesium3DTilesetLifecycleEventReceiver) and spawns a bounding-box collision
 * proxy sized to the tileset.
 */
UCLASS(
    ClassGroup = (Cesium),
    meta = (BlueprintSpawnableComponent),
    HideCategories = (Mobility))
class CESIUMRUNTIME_API UCesiumPointCloudCollisionManager
    : public UActorComponent,
      public ICesium3DTilesetLifecycleEventReceiver {
  GENERATED_BODY()

public:
  UCesiumPointCloudCollisionManager();

  /** Trace channel the proxy blocks. Default: ECC_Visibility. */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Point Cloud Collision")
  TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

  /** Auto-create the proxy when point cloud tiles are detected. */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Point Cloud Collision")
  bool bAutoCreateProxy = true;

  /** Longest-axis voxel resolution for the refine index. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine", meta = (ClampMin = "1"))
  int32 MaxGridDim = 64;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine", meta = (ClampMin = "0.01"))
  float MinVoxelWorldSize = 2.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine", meta = (ClampMin = "0.01"))
  float MaxVoxelWorldSize = 500.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine", meta = (ClampMin = "0.01"))
  float RayPickRadiusFactor = 1.5f;

  /** Per-axis percentile clip for the Tier-1 box (0.01 = 1%). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision", meta = (ClampMin = "0.0", ClampMax = "0.49"))
  float BoxClipFraction = 0.01f;

  /** Cap on captured points per resident tile (downsampled at load). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision", meta = (ClampMin = "1"))
  int32 MaxCapturePoints = 8192;

  /** MAD multiplier for rejecting outlier tiles from the Tier-1 union. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision", meta = (ClampMin = "0.1"))
  float MadThreshold = 3.f;

  /** When false, queries return the Tier-1 box hit only (Phase 1 behavior). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine")
  bool bEnablePreciseRefine = true;

  /** After snapping, slide the hit laterally onto the ray along the local
   * surface plane so the pick sits exactly under the cursor (continuous,
   * density-independent) instead of jumping to the nearest captured point. The
   * result is an interpolated surface position rather than a real point; turn
   * off to return the nearest captured point itself. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine")
  bool bProjectHitToSurface = true;

  /** A candidate is "on the cursor" if its perpendicular distance to the ray is
   * within this multiple of the best candidate's. Among those, the frontmost
   * (nearest) wins, so a genuine nearer surface beats a farther one. Higher =
   * more tolerant of coarse off-axis hits; 1.0 = strictly closest-to-ray. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Refine", meta = (ClampMin = "1.0"))
  float OnCursorPerpFactor = 1.5f;

  /** Debug: on a query, draw the winning tile's voxel grid, highlight the
   * selected voxel in yellow, and mark the returned centroid with a red sphere. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Debug")
  bool bDebugDrawVoxels = false;

  /** Seconds the debug voxel drawing persists after a click. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Point Cloud Collision|Debug", meta = (ClampMin = "0.1"))
  float DebugDrawDuration = 5.f;

  /** The collision proxy, or nullptr if none exists yet. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  UCesiumPointCloudCollisionProxy* GetCollisionProxy() const;

  /** Creates the collision proxy if one doesn't exist. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  void CreateCollisionProxy();

  /** Destroys the collision proxy if one exists. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  void DestroyCollisionProxy();

  /** Recomputes the Tier-1 box from resident tiles (trim + MAD union). */
  void RecomputeTierOneBounds();

  /** Ray-marches resident tiles; returns nearest world hit on this tileset. */
  FCesiumPointCloudHit RefineRay(
      const FVector& WorldOrigin,
      const FVector& WorldDir,
      double MaxDistance);

  /** Test-only: registers a resident tile with explicit local points. */
  void AddResidentTileForTesting(
      UPrimitiveComponent* Component,
      const TArray<FVector3f>& LocalPoints);

protected:
  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void TickComponent(
      float DeltaTime,
      ELevelTick TickType,
      FActorComponentTickFunction* ThisTickFunction) override;

  // ICesium3DTilesetLifecycleEventReceiver
  virtual void OnTileMeshPrimitiveLoaded(
      ICesiumLoadedTilePrimitive& TilePrimitive) override;
  virtual void OnTileLoaded(ICesiumLoadedTile& Tile) override;
  virtual void OnTileVisibilityChanged(
      ICesiumLoadedTile& Tile,
      bool bVisible) override;
  virtual void OnTileUnloading(ICesiumLoadedTile& Tile) override;

private:
  // Reads a capped, downsampled local-space point set from a loaded primitive.
  static TArray<FVector3f> ReadDownsampledLocalPoints(
      class ICesiumLoadedTilePrimitive& TilePrimitive,
      int32 MaxPoints);

  // Reads full-resolution local-space points within Radius of Center from the
  // primitive's glTF POSITION accessor. Empty if the component is not a Cesium
  // primitive, letting callers fall back to the downsampled set.
  static TArray<FVector3f> ReadFullResPointsInWindow(
      class UPrimitiveComponent* Component,
      const FVector& GltfToUnrealScale,
      const FVector& Center,
      double Radius);

  // Debug-draws the winning tile's voxel grid, the cursor ray, the raw
  // (pre-snap) centroid (red) and snapped point (green), and each one's
  // perpendicular miss to the ray. Gated by bDebugDrawVoxels.
  void DebugDrawTileVoxels(
      const FCesiumTilePointIndex& Index,
      const FTransform& TileToWorld,
      const FVector& RawCentroidLocal,
      const FVector& SnappedLocal,
      const FVector& WorldOrigin,
      const FVector& WorldDir) const;

  UPROPERTY(Transient)
  TObjectPtr<UCesiumPointCloudCollisionProxy> CollisionProxy;

  bool bHasPointCloudTiles = false;
  bool bBoundsNeedUpdate = false;

  struct FResidentPointsTile {
    TWeakObjectPtr<UPrimitiveComponent> Component;
    TArray<FVector3f> LocalPoints; // downsampled, scaled to Unreal local space
    // glTF accessor -> Unreal local scale, for reading full-res points at pick
    // time (matches the scaling applied to LocalPoints).
    FVector GltfToUnrealScale = FVector::OneVector;
    FBox TrimmedLocalBox = FBox(ForceInit);
    TUniquePtr<FCesiumTilePointIndex> Index; // lazy
    // Owning tile pointer — stored for identity comparison only, never dereferenced.
    const ICesiumLoadedTile* OwningTile = nullptr;

    FResidentPointsTile();
    ~FResidentPointsTile();
    FResidentPointsTile(FResidentPointsTile&&);
    FResidentPointsTile& operator=(FResidentPointsTile&&);
  };

  TMap<TObjectKey<UPrimitiveComponent>, FResidentPointsTile> ResidentTiles;
};
