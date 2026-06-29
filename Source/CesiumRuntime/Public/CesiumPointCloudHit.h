// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"
#include "CesiumPointCloudHit.generated.h"

class ACesium3DTileset;

/**
 * The result of a point-cloud hit query. Selection unit is the whole Tileset;
 * WorldPoint/WorldNormal/Distance are precise surface data attached to the hit.
 */
USTRUCT(BlueprintType)
struct CESIUMRUNTIME_API FCesiumPointCloudHit {
  GENERATED_BODY()

  UPROPERTY(BlueprintReadOnly, Category = "Cesium|Point Cloud Collision")
  bool bHit = false;

  UPROPERTY(BlueprintReadOnly, Category = "Cesium|Point Cloud Collision")
  ACesium3DTileset* Tileset = nullptr;

  UPROPERTY(BlueprintReadOnly, Category = "Cesium|Point Cloud Collision")
  FVector WorldPoint = FVector::ZeroVector;

  UPROPERTY(BlueprintReadOnly, Category = "Cesium|Point Cloud Collision")
  FVector WorldNormal = FVector::ZeroVector;

  UPROPERTY(BlueprintReadOnly, Category = "Cesium|Point Cloud Collision")
  float Distance = 0.f;
};
