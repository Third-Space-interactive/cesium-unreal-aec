// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Cesium3DTilesetLifecycleEventReceiver.h"
#include "Components/ActorComponent.h"
#include "CesiumPointCloudCollisionManager.generated.h"

class UCesiumPointCloudCollisionProxy;
class ACesium3DTileset;

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

  /** The collision proxy, or nullptr if none exists yet. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  UCesiumPointCloudCollisionProxy* GetCollisionProxy() const;

  /** Creates the collision proxy if one doesn't exist. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  void CreateCollisionProxy();

  /** Destroys the collision proxy if one exists. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  void DestroyCollisionProxy();

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
  UPROPERTY(Transient)
  TObjectPtr<UCesiumPointCloudCollisionProxy> CollisionProxy;

  bool bHasPointCloudTiles = false;
  bool bBoundsNeedUpdate = false;
};
