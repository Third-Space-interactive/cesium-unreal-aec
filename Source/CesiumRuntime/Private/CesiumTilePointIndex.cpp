// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumTilePointIndex.h"

namespace {
// Smallest-eigenvalue eigenvector of a symmetric 3x3 (M = [xx,yy,zz,xy,xz,yz])
// via cyclic Jacobi. Each sweep zeroes the largest off-diagonal using an
// explicit J^T A J multiply through a temp copy, so the update is a correct
// similarity transform for any (including non-axis-aligned) input.
FVector SmallestEigenvector(const double M[6]) {
  double A[3][3] = {
      {M[0], M[3], M[4]},
      {M[3], M[1], M[5]},
      {M[4], M[5], M[2]}};
  double V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

  for (int32 Sweep = 0; Sweep < 16; ++Sweep) {
    int32 p = 0, q = 1;
    double Max = FMath::Abs(A[0][1]);
    if (FMath::Abs(A[0][2]) > Max) { Max = FMath::Abs(A[0][2]); p = 0; q = 2; }
    if (FMath::Abs(A[1][2]) > Max) { Max = FMath::Abs(A[1][2]); p = 1; q = 2; }
    if (Max < 1e-14) break;

    const double Phi = 0.5 * FMath::Atan2(2.0 * A[p][q], A[q][q] - A[p][p]);
    const double c = FMath::Cos(Phi), s = FMath::Sin(Phi);

    double J[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    J[p][p] = c; J[p][q] = -s;
    J[q][p] = s; J[q][q] = c;

    // T = J^T * A
    double T[3][3];
    for (int32 i = 0; i < 3; ++i)
      for (int32 j = 0; j < 3; ++j) {
        double sum = 0.0;
        for (int32 k = 0; k < 3; ++k) sum += J[k][i] * A[k][j];
        T[i][j] = sum;
      }
    // A = T * J
    for (int32 i = 0; i < 3; ++i)
      for (int32 j = 0; j < 3; ++j) {
        double sum = 0.0;
        for (int32 k = 0; k < 3; ++k) sum += T[i][k] * J[k][j];
        A[i][j] = sum;
      }
    // V = V * J
    double Vn[3][3];
    for (int32 i = 0; i < 3; ++i)
      for (int32 j = 0; j < 3; ++j) {
        double sum = 0.0;
        for (int32 k = 0; k < 3; ++k) sum += V[i][k] * J[k][j];
        Vn[i][j] = sum;
      }
    for (int32 i = 0; i < 3; ++i)
      for (int32 j = 0; j < 3; ++j) V[i][j] = Vn[i][j];
  }

  int32 MinIdx = 0;
  double MinVal = A[0][0];
  if (A[1][1] < MinVal) { MinVal = A[1][1]; MinIdx = 1; }
  if (A[2][2] < MinVal) { MinIdx = 2; }
  return FVector(V[0][MinIdx], V[1][MinIdx], V[2][MinIdx]).GetSafeNormal();
}
} // namespace

FIntVector FCesiumTilePointIndex::CellOf(const FVector& P) const {
  const FVector R = (P - GridOrigin) / VoxelEdge;
  return FIntVector(
      FMath::FloorToInt(R.X),
      FMath::FloorToInt(R.Y),
      FMath::FloorToInt(R.Z));
}


TUniquePtr<FCesiumTilePointIndex> FCesiumTilePointIndex::BuildFromLocalPoints(
    TArrayView<const FVector3f> LocalPoints,
    const FCesiumTilePointIndexParams& InParams) {
  if (LocalPoints.Num() == 0) {
    return nullptr;
  }

  TUniquePtr<FCesiumTilePointIndex> Index =
      MakeUnique<FCesiumTilePointIndex>();
  Index->Params = InParams;

  // Raw point bounds set the grid origin and voxel edge.
  FBox Raw(ForceInit);
  for (const FVector3f& P : LocalPoints) {
    Raw += FVector(P);
  }
  Index->GridOrigin = Raw.Min;

  const FVector Size = Raw.GetSize();
  const double MaxComp = FMath::Max3(Size.X, Size.Y, Size.Z);
  Index->VoxelEdge = FMath::Clamp(
      MaxComp / FMath::Max(InParams.MaxGridDim, 1),
      (double)InParams.MinVoxelWorldSize,
      (double)InParams.MaxVoxelWorldSize);
  // Guard against degenerate params (e.g. Min/MaxVoxelWorldSize <= 0) that
  // would make VoxelEdge zero and divide-by-zero in CellOf.
  Index->VoxelEdge = FMath::Max(Index->VoxelEdge, 0.001);

  // Accumulate points into cells.
  for (const FVector3f& Pf : LocalPoints) {
    const FVector P(Pf);
    FCell& Cell = Index->Cells.FindOrAdd(Index->CellOf(P));
    ++Cell.Count;
    Cell.SumPos += P;
    Cell.SumOuter[0] += P.X * P.X;
    Cell.SumOuter[1] += P.Y * P.Y;
    Cell.SumOuter[2] += P.Z * P.Z;
    Cell.SumOuter[3] += P.X * P.Y;
    Cell.SumOuter[4] += P.X * P.Z;
    Cell.SumOuter[5] += P.Y * P.Z;
  }

  // Trim isolated low-count cells (floating noise).
  TArray<FIntVector> ToRemove;
  for (const auto& Pair : Index->Cells) {
    if (Pair.Value.Count >= InParams.MinIsolatedCellCount) {
      continue;
    }
    bool bHasNeighbor = false;
    for (int32 dx = -1; dx <= 1 && !bHasNeighbor; ++dx)
      for (int32 dy = -1; dy <= 1 && !bHasNeighbor; ++dy)
        for (int32 dz = -1; dz <= 1 && !bHasNeighbor; ++dz) {
          if (dx == 0 && dy == 0 && dz == 0)
            continue;
          if (Index->Cells.Contains(
                  Pair.Key + FIntVector(dx, dy, dz)))
            bHasNeighbor = true;
        }
    if (!bHasNeighbor) {
      ToRemove.Add(Pair.Key);
    }
  }
  for (const FIntVector& Key : ToRemove) {
    Index->Cells.Remove(Key);
  }

  // If every cell was trimmed (e.g. all-identical points, zero-extent tile),
  // return null so callers can treat this the same as an empty-input tile.
  if (Index->Cells.Num() == 0) {
    return nullptr;
  }

  // Local bounds over surviving centroids.
  Index->LocalBounds = FBox(ForceInit);
  for (const auto& Pair : Index->Cells) {
    Index->LocalBounds += Pair.Value.Centroid();
  }

  return Index;
}

FCesiumTileRayHit FCesiumTilePointIndex::RayMarch(
    const FVector& LocalOrigin,
    const FVector& LocalDir,
    double MaxDistance) const {
  FCesiumTileRayHit Result;
  if (Cells.Num() == 0) {
    return Result;
  }

  const double PickRadius = Params.RayPickRadiusFactor * VoxelEdge;
  const FVector Dir = LocalDir.GetSafeNormal();
  if (Dir.IsNearlyZero()) {
    return Result;
  }

  // Walk the ray in voxel-sized steps; at each step scan the 3x3x3
  // neighborhood for occupied cells whose centroid is within PickRadius of the
  // ray. DDA order gives front-to-back, so the first qualifying centroid wins.
  const double StepLen = VoxelEdge;
  const int32 MaxSteps =
      (int32)FMath::CeilToDouble(MaxDistance / StepLen) + 2;

  for (int32 Step = 0; Step < MaxSteps; ++Step) {
    const double T = Step * StepLen;
    if (T > MaxDistance) {
      break;
    }
    const FVector SampleP = LocalOrigin + Dir * T;
    const FIntVector Base = CellOf(SampleP);

    double BestT = TNumericLimits<double>::Max();
    const FCell* BestCell = nullptr;

    for (int32 dx = -1; dx <= 1; ++dx)
      for (int32 dy = -1; dy <= 1; ++dy)
        for (int32 dz = -1; dz <= 1; ++dz) {
          const FCell* Cell =
              Cells.Find(Base + FIntVector(dx, dy, dz));
          if (!Cell) {
            continue;
          }
          const FVector C = Cell->Centroid();
          const double Along = FVector::DotProduct(C - LocalOrigin, Dir);
          if (Along < 0.0 || Along > MaxDistance) {
            continue;
          }
          const FVector Closest = LocalOrigin + Dir * Along;
          if (FVector::Dist(Closest, C) > PickRadius) {
            continue;
          }
          if (Along < BestT) {
            BestT = Along;
            BestCell = Cell;
          }
        }

    if (BestCell) {
      Result.bHit = true;
      Result.LocalPoint = BestCell->Centroid();
      Result.Distance = BestT;

      // Aggregate covariance over the hit cell + occupied neighbors.
      const FIntVector HitKey = CellOf(Result.LocalPoint);
      double Cnt = 0.0;
      FVector Sum = FVector::ZeroVector;
      double Outer[6] = {0, 0, 0, 0, 0, 0};
      for (int32 dx = -1; dx <= 1; ++dx)
        for (int32 dy = -1; dy <= 1; ++dy)
          for (int32 dz = -1; dz <= 1; ++dz) {
            const FCell* Cell =
                Cells.Find(HitKey + FIntVector(dx, dy, dz));
            if (!Cell) {
              continue;
            }
            Cnt += Cell->Count;
            Sum += Cell->SumPos;
            for (int32 i = 0; i < 6; ++i) {
              Outer[i] += Cell->SumOuter[i];
            }
          }

      if (Cnt >= 3.0) {
        const FVector Mean = Sum / Cnt;
        const double Cov[6] = {
            Outer[0] / Cnt - Mean.X * Mean.X,
            Outer[1] / Cnt - Mean.Y * Mean.Y,
            Outer[2] / Cnt - Mean.Z * Mean.Z,
            Outer[3] / Cnt - Mean.X * Mean.Y,
            Outer[4] / Cnt - Mean.X * Mean.Z,
            Outer[5] / Cnt - Mean.Y * Mean.Z};
        FVector N = SmallestEigenvector(Cov);
        if (FVector::DotProduct(N, LocalOrigin - Result.LocalPoint) < 0.0) {
          N = -N;
        }
        Result.LocalNormal = N;
      } else {
        // Degenerate neighborhood -> face the viewer.
        Result.LocalNormal = (LocalOrigin - Result.LocalPoint).GetSafeNormal();
      }
      return Result;
    }
  }

  return Result;
}

FCesiumTileRayHit FCesiumTilePointIndex::RefineHitToNearestPoint(
    const FVector& LocalOrigin,
    const FVector& LocalDir,
    const FCesiumTileRayHit& Approx,
    TArrayView<const FVector3f> LocalPoints) const {
  if (!Approx.bHit || LocalPoints.Num() == 0) {
    return Approx;
  }

  const FVector Dir = LocalDir.GetSafeNormal();
  if (Dir.IsNearlyZero()) {
    return Approx;
  }

  // Window: only consider real points near the coarse hit, so we stay on the
  // same front surface. Scaled to the voxel size so it adapts to LOD.
  const double Window = Params.RayPickRadiusFactor * VoxelEdge * 2.0;
  const double WindowSq = Window * Window;

  // On-cursor tolerance: points whose perpendicular miss to the ray is within
  // this are "under the cursor". Among those we take the FRONTMOST (nearest
  // along the ray) so the snap lands on the visible surface rather than a point
  // that merely sits closer to the ray axis but is occluded behind it.
  const double PerpTol = Params.RayPickRadiusFactor * VoxelEdge;
  const double PerpTolSq = PerpTol * PerpTol;
  const double AlongEps = 1e-4 * FMath::Max(VoxelEdge, 1.0);

  // Frontmost on-cursor candidate (preferred), tie-broken by smaller perp.
  double BestAlong = TNumericLimits<double>::Max();
  double BestOnCursorPerpSq = TNumericLimits<double>::Max();
  FVector BestOnCursorPoint = Approx.LocalPoint;
  bool bOnCursor = false;

  // Nearest-to-ray fallback, used only when nothing is under the cursor.
  double BestPerpSq = TNumericLimits<double>::Max();
  FVector BestPerpPoint = Approx.LocalPoint;
  bool bFound = false;

  for (const FVector3f& Pf : LocalPoints) {
    const FVector P(Pf);

    if (FVector::DistSquared(P, Approx.LocalPoint) > WindowSq) {
      continue;
    }

    const FVector ToP = P - LocalOrigin;
    const double Along = FVector::DotProduct(ToP, Dir);
    if (Along < 0.0) {
      continue;
    }

    const FVector Closest = LocalOrigin + Dir * Along;
    const double PerpSq = FVector::DistSquared(Closest, P);

    if (PerpSq < BestPerpSq) {
      BestPerpSq = PerpSq;
      BestPerpPoint = P;
      bFound = true;
    }

    if (PerpSq <= PerpTolSq) {
      const bool bNearerAlongRay = Along < BestAlong - AlongEps;
      const bool bDepthTieCloserToRay =
          FMath::Abs(Along - BestAlong) <= AlongEps &&
          PerpSq < BestOnCursorPerpSq;
      if (!bOnCursor || bNearerAlongRay || bDepthTieCloserToRay) {
        BestAlong = Along;
        BestOnCursorPerpSq = PerpSq;
        BestOnCursorPoint = P;
        bOnCursor = true;
      }
    }
  }

  FCesiumTileRayHit Refined = Approx;
  if (bOnCursor) {
    Refined.LocalPoint = BestOnCursorPoint;
    Refined.Distance = FVector::DotProduct(BestOnCursorPoint - LocalOrigin, Dir);
    // Normal kept from Approx (PCA over the cell neighborhood is still valid).
  } else if (bFound) {
    Refined.LocalPoint = BestPerpPoint;
    Refined.Distance = FVector::DotProduct(BestPerpPoint - LocalOrigin, Dir);
  }
  return Refined;
}

FCesiumTileRayHit FCesiumTilePointIndex::ProjectHitOntoRay(
    const FVector& LocalOrigin,
    const FVector& LocalDir,
    const FCesiumTileRayHit& Hit) {
  if (!Hit.bHit) {
    return Hit;
  }
  const FVector Dir = LocalDir.GetSafeNormal();
  const FVector N = Hit.LocalNormal;
  if (Dir.IsNearlyZero() || N.IsNearlyZero()) {
    return Hit;
  }

  // Intersect the ray with the plane through the hit point with the local
  // surface normal: t = ((P - O) . N) / (Dir . N). The result lies on the ray
  // (exactly under the cursor) and on the surface plane.
  const double Denom = FVector::DotProduct(Dir, N);
  if (FMath::Abs(Denom) < 1e-4) {
    // Grazing ray: intersection is ill-conditioned; keep the snapped point.
    return Hit;
  }
  const double T = FVector::DotProduct(Hit.LocalPoint - LocalOrigin, N) / Denom;
  if (T < 0.0) {
    return Hit;
  }

  FCesiumTileRayHit Projected = Hit;
  Projected.LocalPoint = LocalOrigin + Dir * T;
  Projected.Distance = T;
  return Projected;
}

FVector FCesiumTilePointIndex::CellCenter(const FIntVector& Cell) const {
  return GridOrigin + FVector(
                          (Cell.X + 0.5) * VoxelEdge,
                          (Cell.Y + 0.5) * VoxelEdge,
                          (Cell.Z + 0.5) * VoxelEdge);
}

TArray<FVector> FCesiumTilePointIndex::GetOccupiedCellCenters() const {
  TArray<FVector> Centers;
  Centers.Reserve(Cells.Num());
  for (const auto& Pair : Cells) {
    Centers.Add(CellCenter(Pair.Key));
  }
  return Centers;
}

FVector FCesiumTilePointIndex::GetCellCenterForLocalPoint(
    const FVector& LocalPoint) const {
  return CellCenter(CellOf(LocalPoint));
}
