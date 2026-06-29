// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Components/BoxComponent.h"
#include "CesiumPointCloudCollisionProxy.generated.h"

/**
 * An invisible box collision component sized to a point cloud tileset's
 * bounding volume, acting as a line-trace hit target for the tileset.
 */
UCLASS(ClassGroup = (Cesium), meta = (BlueprintSpawnableComponent))
class CESIUMRUNTIME_API UCesiumPointCloudCollisionProxy
    : public UBoxComponent {
  GENERATED_BODY()

public:
  UCesiumPointCloudCollisionProxy();

  /** Resizes this box to encompass the owning actor's bounds. */
  void UpdateBoundsFromTileset();

  /** Blocks only the given trace channel; ignores all others. */
  void ConfigureCollision(ECollisionChannel TraceChannel);

  /** Centers and sizes this box to the given world-space box. */
  void SetWorldBounds(const FBox& WorldBox);
};
