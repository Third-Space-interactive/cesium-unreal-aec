// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Math/Box.h"
#include "Math/IntVector.h"
#include "Math/Vector.h"
#include "Templates/UniquePtr.h"

/** Tunables for building and querying a per-tile point index. */
struct FCesiumTilePointIndexParams {
  /** Target longest-axis grid resolution; sets voxel edge = extent/MaxGridDim. */
  int32 MaxGridDim = 64;
  /** Clamp floor for voxel edge, in Unreal local units (cm). */
  float MinVoxelWorldSize = 2.f;
  /** Clamp ceiling for voxel edge, in Unreal local units (cm). */
  float MaxVoxelWorldSize = 500.f;
  /** Ray pick tolerance = factor * voxel edge. */
  float RayPickRadiusFactor = 1.5f;
  /** Cells with fewer points than this AND no occupied neighbor are trimmed. */
  int32 MinIsolatedCellCount = 2;
};

/** Result of a ray-march against a tile's points, in the tile's local space. */
struct FCesiumTileRayHit {
  bool bHit = false;
  FVector LocalPoint = FVector::ZeroVector;
  FVector LocalNormal = FVector::ZeroVector;
  double Distance = 0.0;
};

/**
 * A hashed uniform voxel grid over one point-cloud tile's local-space points.
 * Each occupied cell stores a running centroid plus a covariance sum used for
 * normal estimation. Plain C++ (not a UObject) to stay cheap at scale.
 */
class FCesiumTilePointIndex {
public:
  static TUniquePtr<FCesiumTilePointIndex> BuildFromLocalPoints(
      TArrayView<const FVector3f> LocalPoints,
      const FCesiumTilePointIndexParams& Params);

  /** Number of occupied cells after trimming. */
  int32 GetOccupiedCellCount() const { return Cells.Num(); }

  /** Local-space bounds of occupied cell centroids. */
  FBox GetLocalBounds() const { return LocalBounds; }

  double GetVoxelEdge() const { return VoxelEdge; }

  /** Ray-march in local space; LocalDir must be normalized. Added in Task 2. */
  FCesiumTileRayHit RayMarch(
      const FVector& LocalOrigin,
      const FVector& LocalDir,
      double MaxDistance) const;

  /**
   * Snap an approximate ray hit (a voxel centroid from RayMarch) to the actual
   * captured point nearest the ray. Searches only points within a window around
   * the approximate hit, so the refinement is lateral and stays on the same
   * front surface. Points are passed in (the index stores only aggregates).
   */
  FCesiumTileRayHit RefineHitToNearestPoint(
      const FVector& LocalOrigin,
      const FVector& LocalDir,
      const FCesiumTileRayHit& Approx,
      TArrayView<const FVector3f> LocalPoints) const;

  /**
   * Slide a hit onto the ray by intersecting the ray with the local surface
   * plane (through the hit point, using the hit normal), so the result sits
   * exactly under the cursor instead of at the nearest captured point - accuracy
   * no longer depends on point density. Returns the hit unchanged for grazing
   * rays where the intersection is ill-conditioned. Pure geometry; static.
   */
  static FCesiumTileRayHit ProjectHitOntoRay(
      const FVector& LocalOrigin,
      const FVector& LocalDir,
      const FCesiumTileRayHit& Hit);

  /** Local-space centers of all occupied voxels (debug visualization). */
  TArray<FVector> GetOccupiedCellCenters() const;

  /** Local-space center of the voxel containing the given local point. */
  FVector GetCellCenterForLocalPoint(const FVector& LocalPoint) const;

private:
  struct FCell {
    int32 Count = 0;
    FVector SumPos = FVector::ZeroVector;
    // Raw upper-triangular second-moment sums (xx,yy,zz,xy,xz,yz); NOT
    // mean-centered. Covariance = SumOuter/Count - centroid (outer) centroid.
    double SumOuter[6] = {0, 0, 0, 0, 0, 0};
    FVector Centroid() const { return SumPos / FMath::Max(Count, 1); }
  };

  FIntVector CellOf(const FVector& P) const;
  FVector CellCenter(const FIntVector& Cell) const;

  TMap<FIntVector, FCell> Cells;
  FVector GridOrigin = FVector::ZeroVector;
  double VoxelEdge = 1.0;
  FBox LocalBounds = FBox(ForceInit);
  FCesiumTilePointIndexParams Params;
};
