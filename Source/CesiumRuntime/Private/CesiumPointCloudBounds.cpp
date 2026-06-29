// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudBounds.h"
#include "Algo/Sort.h"

namespace {
// Index an already-sorted array at a percentile fraction in [0, 1].
double PercentileSorted(const TArray<double>& Sorted, float Fraction) {
  if (Sorted.Num() == 0) {
    return 0.0;
  }
  const int32 Idx = FMath::Clamp(
      (int32)(Fraction * (Sorted.Num() - 1)),
      0,
      Sorted.Num() - 1);
  return Sorted[Idx];
}

double Median(TArray<double> Values) {
  if (Values.Num() == 0) {
    return 0.0;
  }
  Algo::Sort(Values);
  const int32 Mid = Values.Num() / 2;
  if (Values.Num() % 2 == 0) {
    return (Values[Mid - 1] + Values[Mid]) * 0.5;
  }
  return Values[Mid];
}
} // namespace

namespace CesiumPointCloudBounds {

FBox ComputeTrimmedLocalBox(
    TArrayView<const FVector3f> LocalPoints,
    float ClipFraction) {
  if (LocalPoints.Num() == 0) {
    return FBox(ForceInit);
  }
  const float Clip = FMath::Clamp(ClipFraction, 0.f, 0.49f);

  TArray<double> Xs, Ys, Zs;
  Xs.Reserve(LocalPoints.Num());
  Ys.Reserve(LocalPoints.Num());
  Zs.Reserve(LocalPoints.Num());
  for (const FVector3f& P : LocalPoints) {
    Xs.Add(P.X);
    Ys.Add(P.Y);
    Zs.Add(P.Z);
  }

  // Sort each axis once, then read both percentile ends from it.
  Algo::Sort(Xs);
  Algo::Sort(Ys);
  Algo::Sort(Zs);
  const FVector Min(
      PercentileSorted(Xs, Clip),
      PercentileSorted(Ys, Clip),
      PercentileSorted(Zs, Clip));
  const FVector Max(
      PercentileSorted(Xs, 1.f - Clip),
      PercentileSorted(Ys, 1.f - Clip),
      PercentileSorted(Zs, 1.f - Clip));
  return FBox(Min, Max);
}

FBox UnionWithMadRejection(TArrayView<const FBox> Boxes, float MadThreshold) {
  if (Boxes.Num() == 0) {
    return FBox(ForceInit);
  }
  if (Boxes.Num() < 4) {
    // Too few to estimate a robust spread; just union them.
    FBox U(ForceInit);
    for (const FBox& B : Boxes) {
      U += B;
    }
    return U;
  }

  // Compute each box center once; reused for the median, deviations, and test.
  TArray<FVector> Centers;
  Centers.Reserve(Boxes.Num());
  for (const FBox& B : Boxes) {
    Centers.Add(B.GetCenter());
  }

  TArray<double> Cx, Cy, Cz;
  for (const FVector& C : Centers) {
    Cx.Add(C.X);
    Cy.Add(C.Y);
    Cz.Add(C.Z);
  }
  const FVector Med(Median(Cx), Median(Cy), Median(Cz));

  TArray<double> Dx, Dy, Dz;
  for (const FVector& C : Centers) {
    Dx.Add(FMath::Abs(C.X - Med.X));
    Dy.Add(FMath::Abs(C.Y - Med.Y));
    Dz.Add(FMath::Abs(C.Z - Med.Z));
  }
  // Floor (UE world units) prevents an all-but-zero MAD from rejecting every tile when tile centers are near-perfectly aligned.
  const FVector Mad(
      FMath::Max(Median(Dx), 1.0),
      FMath::Max(Median(Dy), 1.0),
      FMath::Max(Median(Dz), 1.0));

  FBox U(ForceInit);
  for (int32 i = 0; i < Boxes.Num(); ++i) {
    const FVector& C = Centers[i];
    if (FMath::Abs(C.X - Med.X) > MadThreshold * Mad.X ||
        FMath::Abs(C.Y - Med.Y) > MadThreshold * Mad.Y ||
        FMath::Abs(C.Z - Med.Z) > MadThreshold * Mad.Z) {
      continue; // reject outlier tile
    }
    U += Boxes[i];
  }
  // Guard: if everything was rejected, fall back to a full union.
  if (!U.IsValid) {
    for (const FBox& B : Boxes) {
      U += B;
    }
  }
  return U;
}

} // namespace CesiumPointCloudBounds
