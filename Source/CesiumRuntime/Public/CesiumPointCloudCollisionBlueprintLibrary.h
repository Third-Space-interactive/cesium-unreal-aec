// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CesiumPointCloudCollisionBlueprintLibrary.generated.h"

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
};
