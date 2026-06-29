// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudBounds.h"
#include "CesiumTilePointIndex.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
    FCesiumTilePointIndexSpec,
    "Cesium.Unit.PointCloudCollision.TilePointIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::ServerContext |
        EAutomationTestFlags::CommandletContext |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCesiumTilePointIndexSpec)

void FCesiumTilePointIndexSpec::Define() {
  Describe("BuildFromLocalPoints", [this]() {
    It("collapses points in the same voxel into one occupied cell", [this]() {
      // A 100cm cube grid of points spaced 100cm apart, 5x5x5 = 125 points.
      TArray<FVector3f> Points;
      for (int32 X = 0; X < 5; ++X)
        for (int32 Y = 0; Y < 5; ++Y)
          for (int32 Z = 0; Z < 5; ++Z)
            Points.Add(FVector3f(X * 100.f, Y * 100.f, Z * 100.f));

      FCesiumTilePointIndexParams Params;
      Params.MaxGridDim = 64; // edge = 400/64 ~= 6.25 -> clamped to >= 2
      Params.MinIsolatedCellCount = 1; // each cell has exactly 1 point; disable outlier trim
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);

      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;
      // Each point is far apart relative to voxel edge -> 125 occupied cells.
      TestEqual(TEXT("Occupied cells"), Index->GetOccupiedCellCount(), 125);
    });

    It("trims isolated low-count outlier cells", [this]() {
      // Dense clean cluster (a 10x10 plane at z=0, spacing 10cm) + 1 far outlier.
      TArray<FVector3f> Points;
      for (int32 X = 0; X < 10; ++X)
        for (int32 Y = 0; Y < 10; ++Y)
          Points.Add(FVector3f(X * 10.f, Y * 10.f, 0.f));
      Points.Add(FVector3f(100000.f, 100000.f, 100000.f)); // lone noise point

      FCesiumTilePointIndexParams Params;
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);

      // The outlier sits alone in its cell -> trimmed. Clean bounds remain small.
      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;
      FBox Bounds = Index->GetLocalBounds();
      TestTrue(
          TEXT("Bounds exclude outlier"),
          Bounds.Max.X < 1000.0 && Bounds.Max.Y < 1000.0 &&
              Bounds.Max.Z < 1000.0);
    });

    It("returns null when all points collapse to a single trimmed cell", [this]() {
      // All-identical points -> one cell, Count below MinIsolatedCellCount,
      // no neighbors -> trimmed -> no cells survive -> null.
      TArray<FVector3f> Points;
      for (int32 i = 0; i < 1; ++i)
        Points.Add(FVector3f(5.f, 5.f, 5.f));
      FCesiumTilePointIndexParams Params;
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);
      TestNull(TEXT("Degenerate tile -> null"), Index.Get());
    });
  });

  Describe("RayMarch", [this]() {
    It("hits a horizontal plane and returns an up-ish normal", [this]() {
      // Dense plane at z=0, 40x40 @ 5cm spacing, centered near origin.
      TArray<FVector3f> Points;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Points.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));

      FCesiumTilePointIndexParams Params;
      Params.MinIsolatedCellCount = 1; // keep all (dense anyway)
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);

      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;
      // Ray straight down through the plane from above the center.
      FCesiumTileRayHit Hit = Index->RayMarch(
          FVector(0, 0, 500), FVector(0, 0, -1), 10000.0);

      TestTrue(TEXT("Hit"), Hit.bHit);
      TestTrue(TEXT("Near z=0"), FMath::Abs(Hit.LocalPoint.Z) < 20.0);
      // Plane normal is +/- Z; oriented to the viewer (above) -> +Z.
      TestTrue(TEXT("Normal up"), Hit.LocalNormal.Z > 0.7);
      TestTrue(TEXT("Distance near 500"), FMath::Abs(Hit.Distance - 500.0) < 30.0);
    });

    It("misses when the ray passes through empty space", [this]() {
      TArray<FVector3f> Points;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Points.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));

      FCesiumTilePointIndexParams Params;
      Params.MinIsolatedCellCount = 1;
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);

      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;
      // Ray parallel to the plane, well above it -> no hit.
      FCesiumTileRayHit Hit = Index->RayMarch(
          FVector(-1000, 0, 500), FVector(1, 0, 0), 10000.0);
      TestFalse(TEXT("No hit"), Hit.bHit);
    });

    It("returns the correct normal on a tilted (45-degree) plane", [this]() {
      // Plane z = x: points (x, y, x). True normal is (-1,0,1)/sqrt2.
      TArray<FVector3f> Points;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Points.Add(FVector3f(X * 5.f, Y * 5.f, X * 5.f));

      FCesiumTilePointIndexParams Params;
      Params.MinIsolatedCellCount = 1;
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);

      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;
      // Ray straight down through the origin; hits the plane at ~(0,*,0).
      FCesiumTileRayHit Hit =
          Index->RayMarch(FVector(0, 0, 500), FVector(0, 0, -1), 10000.0);

      TestTrue(TEXT("Hit"), Hit.bHit);
      // Normal perpendicular to z=x, oriented toward the viewer above: ~(-0.707,0,0.707).
      TestTrue(TEXT("Normal X negative"), Hit.LocalNormal.X < -0.5);
      TestTrue(TEXT("Normal Z positive"), Hit.LocalNormal.Z > 0.5);
    });

    It("snaps the hit to the real point nearest the ray, not the cell centroid",
       [this]() {
      // Three points in one coarse voxel, spread along +X. Their centroid is at
      // X=20, but the point nearest an X=0 ray is at X=0.
      TArray<FVector3f> Points;
      Points.Add(FVector3f(0.f, 0.f, 0.f));
      Points.Add(FVector3f(20.f, 0.f, 0.f));
      Points.Add(FVector3f(40.f, 0.f, 0.f));

      FCesiumTilePointIndexParams Params;
      Params.MaxGridDim = 1;             // force a single coarse voxel...
      Params.MinVoxelWorldSize = 100.f;  // ...edge clamps to 100 so all 3 share it
      Params.MaxVoxelWorldSize = 1000.f;
      Params.MinIsolatedCellCount = 1;   // don't trim the single cell

      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);
      TestNotNull(TEXT("Index built"), Index.Get());

      const FVector Origin(0, 0, 500);
      const FVector Dir(0, 0, -1);

      // Coarse march returns the centroid (~X=20).
      FCesiumTileRayHit Coarse = Index->RayMarch(Origin, Dir, 10000.0);
      TestTrue(TEXT("Coarse hit"), Coarse.bHit);
      TestTrue(TEXT("Centroid is off-axis"), Coarse.LocalPoint.X > 10.0);

      // Refinement snaps to the real on-axis point (~X=0).
      FCesiumTileRayHit Refined =
          Index->RefineHitToNearestPoint(Origin, Dir, Coarse, Points);
      TestTrue(TEXT("Refined hit"), Refined.bHit);
      TestTrue(
          TEXT("Refined snaps onto the ray"),
          FMath::Abs(Refined.LocalPoint.X) < 1.0);
    });

    It("snaps to the frontmost surface point, not a closer-to-axis point behind it",
       [this]() {
      // Two real points that both sit under the cursor (within the on-cursor
      // tolerance of the ray), inside one coarse voxel:
      //   - Exterior surface: slightly off-axis (X=3) but in FRONT (Z=+100).
      //   - Interior:         exactly on-axis (X=0) but BEHIND it (Z=-100).
      // A pure minimum-perpendicular metric wrongly snaps to the interior point
      // (perp 0). An occlusion-aware (frontmost-under-cursor) snap must pick the
      // exterior front point, because the ray reaches it first. This is the
      // "snaps into the building interior" defect.
      TArray<FVector3f> Points;
      Points.Add(FVector3f(3.f, 0.f, 100.f));   // exterior, in front
      Points.Add(FVector3f(0.f, 0.f, -100.f));  // interior, on-axis, behind

      FCesiumTilePointIndexParams Params;
      Params.MaxGridDim = 1;             // single coarse voxel...
      Params.MinVoxelWorldSize = 300.f;  // ...edge clamps to 300 so both share it
      Params.MaxVoxelWorldSize = 1000.f;
      Params.MinIsolatedCellCount = 1;   // don't trim the single cell

      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);
      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;

      const FVector Origin(0, 0, 500);
      const FVector Dir(0, 0, -1);

      FCesiumTileRayHit Coarse = Index->RayMarch(Origin, Dir, 10000.0);
      TestTrue(TEXT("Coarse hit"), Coarse.bHit);

      FCesiumTileRayHit Refined =
          Index->RefineHitToNearestPoint(Origin, Dir, Coarse, Points);
      TestTrue(TEXT("Refined hit"), Refined.bHit);
      // The snap lands on the FRONT exterior point (Z ~ +100), not the interior
      // on-axis point (Z ~ -100) that a minimum-perpendicular metric would pick.
      TestTrue(
          TEXT("Snaps to the frontmost surface, not through it"),
          Refined.LocalPoint.Z > 50.0);
    });
  });

  Describe("ProjectHitOntoRay", [this]() {
    It("slides the hit along the surface plane to sit exactly under the ray",
       [this]() {
      // Dense horizontal plane z=0 (40x40 @ 5cm). A vertical ray offset in X
      // should resolve to the surface point directly under it, even though the
      // nearest captured point is a grid step to the side.
      TArray<FVector3f> Points;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Points.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));

      FCesiumTilePointIndexParams Params;
      Params.MinIsolatedCellCount = 1;
      TUniquePtr<FCesiumTilePointIndex> Index =
          FCesiumTilePointIndex::BuildFromLocalPoints(Points, Params);
      TestNotNull(TEXT("Index built"), Index.Get());
      if (!Index.Get()) return;

      // Ray straight down through X=37 (between grid points at 35 and 40).
      const FVector Origin(37, 0, 500);
      const FVector Dir(0, 0, -1);

      FCesiumTileRayHit Coarse = Index->RayMarch(Origin, Dir, 10000.0);
      TestTrue(TEXT("Coarse hit"), Coarse.bHit);
      FCesiumTileRayHit Snapped =
          Index->RefineHitToNearestPoint(Origin, Dir, Coarse, Points);

      // Surface projection slides laterally onto the ray (X=37), staying on the
      // plane (Z=0) - not the nearest captured point at X=35.
      FCesiumTileRayHit Surface =
          FCesiumTilePointIndex::ProjectHitOntoRay(Origin, Dir, Snapped);
      TestTrue(TEXT("Surface hit"), Surface.bHit);
      TestTrue(
          TEXT("Sits under the ray in X"),
          FMath::Abs(Surface.LocalPoint.X - 37.0) < 0.5);
      TestTrue(
          TEXT("Stays on the surface in Z"),
          FMath::Abs(Surface.LocalPoint.Z) < 0.5);
    });
  });

  Describe("ComputeTrimmedLocalBox", [this]() {
    It("excludes a sparse outlier so the box matches the clean cluster", [this]() {
      // Clean 4276 x 4232-ish cluster + a few extreme outliers.
      TArray<FVector3f> Points;
      for (int32 i = 0; i < 1000; ++i) {
        const float t = (float)i / 1000.f;
        Points.Add(FVector3f(t * 4276.f, t * 4232.f, t * 1525.f));
      }
      Points.Add(FVector3f(31360.f, 0.f, 0.f));
      Points.Add(FVector3f(0.f, 34303.f, 0.f));
      Points.Add(FVector3f(0.f, 0.f, 3167.f));

      FBox Box = CesiumPointCloudBounds::ComputeTrimmedLocalBox(Points, 0.01f);
      TestTrue(TEXT("X trimmed"), Box.Max.X < 6000.0);
      TestTrue(TEXT("Y trimmed"), Box.Max.Y < 6000.0);
      TestTrue(TEXT("Z trimmed"), Box.Max.Z < 2000.0);
    });
  });

  Describe("UnionWithMadRejection", [this]() {
    It("drops a far outlier box from the union", [this]() {
      TArray<FBox> Boxes;
      Boxes.Add(FBox(FVector(0), FVector(100)));
      Boxes.Add(FBox(FVector(50), FVector(150)));
      Boxes.Add(FBox(FVector(60), FVector(160)));
      Boxes.Add(FBox(FVector(100000), FVector(100100))); // outlier tile

      FBox U = CesiumPointCloudBounds::UnionWithMadRejection(Boxes, 3.0f);
      TestTrue(TEXT("Outlier dropped"), U.Max.X < 1000.0);
    });
  });
}
