// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumPointCloudHit.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CesiumPointCloudCollisionBlueprintLibrary.generated.h"

class APlayerController;

/** Blueprint utilities for managing point cloud collision across all tilesets. */
UCLASS()
class CESIUMRUNTIME_API UCesiumPointCloudCollisionBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  /** Adds a manager to every tileset in the world that lacks one. */
  UFUNCTION(
      BlueprintCallable,
      Category = "Cesium|Point Cloud Collision",
      meta = (WorldContext = "WorldContextObject"))
  static void AddCollisionManagerToAllTilesets(
      const UObject* WorldContextObject);

  /** Removes the manager from every tileset in the world. */
  UFUNCTION(
      BlueprintCallable,
      Category = "Cesium|Point Cloud Collision",
      meta = (WorldContext = "WorldContextObject"))
  static void RemoveCollisionManagerFromAllTilesets(
      const UObject* WorldContextObject);

  /** Sets the trace channel on all existing managers in the world. */
  UFUNCTION(
      BlueprintCallable,
      Category = "Cesium|Point Cloud Collision",
      meta = (WorldContext = "WorldContextObject"))
  static void SetCollisionChannelForAllManagers(
      const UObject* WorldContextObject,
      ECollisionChannel Channel);

  /**
   * Hit-tests point clouds along a world-space ray. Runs a Tier-1 multi-hit
   * broadphase trace, then refines each candidate tileset against its resident
   * tile points, returning the nearest real-surface hit.
   */
  UFUNCTION(
      BlueprintCallable,
      Category = "Cesium|Point Cloud Collision",
      meta = (WorldContext = "WorldContextObject"))
  static FCesiumPointCloudHit QueryPointCloudAlongRay(
      UObject* WorldContextObject,
      FVector Origin,
      FVector Direction,
      float MaxDistance,
      ECollisionChannel TraceChannel = ECC_Visibility);

  /** Convenience wrapper that builds the ray from the cursor. */
  UFUNCTION(BlueprintCallable, Category = "Cesium|Point Cloud Collision")
  static FCesiumPointCloudHit QueryPointCloudUnderCursor(
      APlayerController* PlayerController,
      float MaxDistance);
};
