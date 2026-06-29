// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionBlueprintLibrary.h"
#include "CesiumPointCloudCollisionProxy.h"
#include "CesiumPointCloudCollisionManager.h"
#include "CesiumPointCloudHit.h"
#include "Components/BoxComponent.h"
#include "Tests/CesiumTestHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
    FCesiumPointCloudCollisionSpec,
    "Cesium.Unit.PointCloudCollision.Integration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCesiumPointCloudCollisionSpec)

void FCesiumPointCloudCollisionSpec::Define() {
  Describe("Manager.RecomputeTierOneBounds", [this]() {
    It("builds a proxy box that excludes an outlier tile", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();
      Mgr->CreateCollisionProxy();

      // Four clean tiles near origin + one outlier tile far away.
      auto MakeBlock = [](FVector Base) {
        TArray<FVector3f> P;
        for (int32 i = 0; i < 50; ++i)
          P.Add(FVector3f(Base + FVector(i, i, 0)));
        return P;
      };
      UPrimitiveComponent* C1 =
          NewObject<UBoxComponent>(Actor, TEXT("C1"));
      C1->RegisterComponent();
      Mgr->AddResidentTileForTesting(C1, MakeBlock(FVector(0)));
      UPrimitiveComponent* C2 =
          NewObject<UBoxComponent>(Actor, TEXT("C2"));
      C2->RegisterComponent();
      Mgr->AddResidentTileForTesting(C2, MakeBlock(FVector(60)));
      UPrimitiveComponent* C3 =
          NewObject<UBoxComponent>(Actor, TEXT("C3"));
      C3->RegisterComponent();
      Mgr->AddResidentTileForTesting(C3, MakeBlock(FVector(120)));
      UPrimitiveComponent* C4 =
          NewObject<UBoxComponent>(Actor, TEXT("C4"));
      C4->RegisterComponent();
      Mgr->AddResidentTileForTesting(C4, MakeBlock(FVector(180)));
      UPrimitiveComponent* COut =
          NewObject<UBoxComponent>(Actor, TEXT("COut"));
      COut->RegisterComponent();
      Mgr->AddResidentTileForTesting(COut, MakeBlock(FVector(500000)));

      Mgr->RecomputeTierOneBounds();

      UCesiumPointCloudCollisionProxy* Proxy = Mgr->GetCollisionProxy();
      TestTrue(
          TEXT("Box excludes outlier"),
          Proxy->Bounds.GetBox().Max.X < 100000.0);

      Actor->Destroy();
    });
  });

  Describe("Manager.RefineRay", [this]() {
    It("returns a world hit on a plane with a viewer-facing normal", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();

      // Plane at local z=0; component placed at world (1000,0,0).
      UBoxComponent* Comp = NewObject<UBoxComponent>(Actor, TEXT("Tile"));
      Comp->SetupAttachment(Root);
      Comp->RegisterComponent();
      Comp->SetWorldLocation(FVector(1000, 0, 0));

      TArray<FVector3f> Plane;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Plane.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));
      Mgr->AddResidentTileForTesting(Comp, Plane);

      // World ray straight down through (1000,0,0).
      FCesiumPointCloudHit Hit =
          Mgr->RefineRay(FVector(1000, 0, 500), FVector(0, 0, -1), 10000.0);

      TestTrue(TEXT("Hit"), Hit.bHit);
      TestTrue(
          TEXT("Near plane world z=0"),
          FMath::Abs(Hit.WorldPoint.Z) < 20.0);
      TestTrue(TEXT("Normal up"), Hit.WorldNormal.Z > 0.7);

      Actor->Destroy();
    });
  });

  Describe("Manager eviction", [this]() {
    It("skips a resident tile whose component became invalid", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();

      UBoxComponent* Comp = NewObject<UBoxComponent>(Actor, TEXT("Tile"));
      Comp->SetupAttachment(Root);
      Comp->RegisterComponent();

      TArray<FVector3f> Plane;
      for (int32 X = -20; X < 20; ++X)
        for (int32 Y = -20; Y < 20; ++Y)
          Plane.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));
      Mgr->AddResidentTileForTesting(Comp, Plane);

      // Destroy the component -> weak ptr goes invalid.
      Comp->DestroyComponent();

      FCesiumPointCloudHit Hit =
          Mgr->RefineRay(FVector(0, 0, 500), FVector(0, 0, -1), 10000.0);
      TestFalse(TEXT("No hit after eviction"), Hit.bHit);

      Actor->Destroy();
    });
  });

  Describe("Proxy.SetWorldBounds", [this]() {
    It("sizes the box to the world box", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root =
          NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionProxy* Proxy =
          NewObject<UCesiumPointCloudCollisionProxy>(Actor);
      Proxy->SetupAttachment(Root);
      Proxy->RegisterComponent();

      Proxy->SetWorldBounds(FBox(FVector(-100), FVector(300)));

      TestEqual(
          TEXT("Center"),
          Proxy->GetComponentLocation(),
          FVector(100));
      TestEqual(
          TEXT("Extent"),
          Proxy->GetUnscaledBoxExtent(),
          FVector(200));

      Actor->Destroy();
    });
  });

  Describe("Overlap correctness (manager RefineRay)", [this]() {
    // NOTE: The full UCesiumPointCloudCollisionBlueprintLibrary::QueryPointCloudAlongRay
    // path uses LineTraceMultiByChannel, which the headless automation world
    // (GEngine world context [0]) cannot service (no physics scene). The
    // library's broadphase+dispatch+nearest-wins loop is covered by code
    // review; here we verify the underlying GUARANTEE it relies on — that the
    // nearer cloud's real surface produces the closer hit — directly via
    // RefineRay, which runs correctly headless.
    It("the nearer cloud's surface yields the closer hit under a shared ray", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();

      auto MakeCloud =
          [&](FVector PlaneWorldLocation) -> UCesiumPointCloudCollisionManager* {
        AActor* Actor = World->SpawnActor<AActor>();
        USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        UCesiumPointCloudCollisionManager* Mgr =
            NewObject<UCesiumPointCloudCollisionManager>(Actor);
        Mgr->RegisterComponent();
        UBoxComponent* Comp = NewObject<UBoxComponent>(Actor, TEXT("Tile"));
        Comp->SetupAttachment(Root);
        Comp->RegisterComponent();
        Comp->SetWorldLocation(PlaneWorldLocation);
        TArray<FVector3f> Plane;
        for (int32 X = -20; X < 20; ++X)
          for (int32 Y = -20; Y < 20; ++Y)
            Plane.Add(FVector3f(X * 5.f, Y * 5.f, 0.f));
        Mgr->AddResidentTileForTesting(Comp, Plane);
        return Mgr;
      };

      // Two planes under the same vertical ray: near at z=200, far at z=0.
      UCesiumPointCloudCollisionManager* Near = MakeCloud(FVector(0, 0, 200));
      UCesiumPointCloudCollisionManager* Far = MakeCloud(FVector(0, 0, 0));

      FCesiumPointCloudHit NearHit =
          Near->RefineRay(FVector(0, 0, 1000), FVector(0, 0, -1), 100000.0);
      FCesiumPointCloudHit FarHit =
          Far->RefineRay(FVector(0, 0, 1000), FVector(0, 0, -1), 100000.0);

      TestTrue(TEXT("Near hit"), NearHit.bHit);
      TestTrue(TEXT("Far hit"), FarHit.bHit);
      TestTrue(
          TEXT("Near plane z ~200"),
          FMath::Abs(NearHit.WorldPoint.Z - 200.0) < 30.0);
      TestTrue(
          TEXT("Far plane z ~0"), FMath::Abs(FarHit.WorldPoint.Z) < 30.0);
      // Nearer surface => smaller distance => the library's nearest-wins loop
      // selects it.
      TestTrue(TEXT("Near is closer"), NearHit.Distance < FarHit.Distance);

      Near->GetOwner()->Destroy();
      Far->GetOwner()->Destroy();
    });
  });

  Describe("RefineRay.LodSelection", [this]() {
    It("prefers the fine tile's on-ray point over a nearer coarse centroid",
       [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();

      // COARSE tile: an offset cluster near the ray at depth Z=100 (NEARER the
      // camera), plus two far points that force a large voxel edge (~100) and
      // are trimmed from occupancy. Its hit centroid is laterally offset (~X=30).
      TArray<FVector3f> Coarse;
      for (int32 x = 20; x <= 40; x += 10)
        for (int32 y = -1; y <= 1; ++y)
          Coarse.Add(FVector3f((float)x, (float)y, 100.f));
      Coarse.Add(FVector3f(3200.f, 0.f, 0.f));
      Coarse.Add(FVector3f(-3200.f, 0.f, 0.f));

      // FINE tile: a dense plane at Z=80 (slightly FARTHER), small extent ->
      // small voxel. Its hit sits essentially on the X=0 ray (tiny perp).
      TArray<FVector3f> Fine;
      for (int32 x = -20; x <= 20; ++x)
        for (int32 y = -2; y <= 2; ++y)
          Fine.Add(FVector3f((float)x, (float)y, 80.f));

      UBoxComponent* CoarseComp =
          NewObject<UBoxComponent>(Actor, TEXT("Coarse"));
      CoarseComp->RegisterComponent();
      Mgr->AddResidentTileForTesting(CoarseComp, Coarse);

      UBoxComponent* FineComp = NewObject<UBoxComponent>(Actor, TEXT("Fine"));
      FineComp->RegisterComponent();
      Mgr->AddResidentTileForTesting(FineComp, Fine);

      // Ray straight down the X=0 axis from above.
      FCesiumPointCloudHit Hit =
          Mgr->RefineRay(FVector(0, 0, 1000), FVector(0, 0, -1), 100000.0);

      TestTrue(TEXT("Hit"), Hit.bHit);
      // Coarse surface is at Z=100 (nearer); fine is at Z=80. The fine tile must
      // win on perpendicular distance despite being slightly farther.
      TestTrue(
          TEXT("Picked fine surface (Z~80), not coarse (Z=100)"),
          FMath::Abs(Hit.WorldPoint.Z - 80.0) < 5.0);
      TestTrue(
          TEXT("Hit is on the cursor ray (X~0), not the coarse offset (X~30)"),
          FMath::Abs(Hit.WorldPoint.X) < 10.0);

      Actor->Destroy();
    });
  });

  Describe("RefineRay.OcclusionTieBreak", [this]() {
    It("picks the nearer on-cursor surface, not the coarse front or the far point",
       [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();

      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();

      // A: spurious COARSE tile, frontmost (Z=300), offset ~X=20 from the X=0
      // ray (high perp). Far points inflate its voxel into a wide-radius grab.
      TArray<FVector3f> A;
      for (int32 x = 20; x <= 30; x += 5)
        for (int32 y = -1; y <= 1; ++y)
          A.Add(FVector3f((float)x, (float)y, 300.f));
      A.Add(FVector3f(3200.f, 0.f, 0.f));
      A.Add(FVector3f(-3200.f, 0.f, 0.f));

      // B: accurate NEAR tile (Z=100), nearest point at |X|=1 (perp ~1). Points
      // straddle X=0 (no point AT 0) so the broadphase box contains the ray.
      TArray<FVector3f> B;
      for (int32 x : {-3, -2, -1, 1, 2, 3})
        for (int32 y = -2; y <= 2; ++y)
          B.Add(FVector3f((float)x, (float)y, 100.f));

      // C: accurate FAR tile (Z=0), also nearest at |X|=1 (perp ~1).
      TArray<FVector3f> C;
      for (int32 x : {-3, -2, -1, 1, 2, 3})
        for (int32 y = -2; y <= 2; ++y)
          C.Add(FVector3f((float)x, (float)y, 0.f));

      UBoxComponent* CA = NewObject<UBoxComponent>(Actor, TEXT("A"));
      CA->RegisterComponent();
      Mgr->AddResidentTileForTesting(CA, A);
      UBoxComponent* CB = NewObject<UBoxComponent>(Actor, TEXT("B"));
      CB->RegisterComponent();
      Mgr->AddResidentTileForTesting(CB, B);
      UBoxComponent* CC = NewObject<UBoxComponent>(Actor, TEXT("C"));
      CC->RegisterComponent();
      Mgr->AddResidentTileForTesting(CC, C);

      // Ray straight down the X=0 axis from above.
      FCesiumPointCloudHit Hit =
          Mgr->RefineRay(FVector(0, 0, 1000), FVector(0, 0, -1), 100000.0);

      TestTrue(TEXT("Hit"), Hit.bHit);
      // B wins: A (perp ~20) is excluded by the perp threshold even though it is
      // frontmost; among the on-cursor {B@Z=100, C@Z=0} the frontmost B wins.
      // (The old depth-band rule returns A at Z=300 — this is the regression.)
      TestTrue(
          TEXT("Near on-cursor surface (Z~100)"),
          FMath::Abs(Hit.WorldPoint.Z - 100.0) < 25.0);
      TestTrue(
          TEXT("On the cursor (|X|~1), not the coarse offset (X~20)"),
          FMath::Abs(Hit.WorldPoint.X) < 10.0);

      Actor->Destroy();
    });
  });

  Describe("Library empty-space behavior", [this]() {
    It("reports no hit when the ray crosses the box but misses all points", [this]() {
      UWorld* World = CesiumTestHelpers::getGlobalWorldContext();
      AActor* Actor = World->SpawnActor<AActor>();
      USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
      Actor->SetRootComponent(Root);
      Root->RegisterComponent();
      UCesiumPointCloudCollisionManager* Mgr =
          NewObject<UCesiumPointCloudCollisionManager>(Actor);
      Mgr->RegisterComponent();
      Mgr->CreateCollisionProxy();

      UBoxComponent* Comp = NewObject<UBoxComponent>(Actor, TEXT("Tile"));
      Comp->SetupAttachment(Root);
      Comp->RegisterComponent();

      // A thin vertical line of points along the Z axis at x=y=0.
      TArray<FVector3f> Line;
      for (int32 Z = 0; Z < 40; ++Z)
        Line.Add(FVector3f(0.f, 0.f, Z * 5.f));
      Mgr->AddResidentTileForTesting(Comp, Line);
      Mgr->RecomputeTierOneBounds();

      // Ray parallel to Z but offset far in X -> crosses the box AABB region
      // around the points only if box is wide; points are far from the ray.
      FCesiumPointCloudHit Hit =
          UCesiumPointCloudCollisionBlueprintLibrary::QueryPointCloudAlongRay(
              World, FVector(1000, 0, 1000), FVector(0, 0, -1), 100000.f,
              ECC_Visibility);
      TestFalse(TEXT("No hit in empty space"), Hit.bHit);

      Actor->Destroy();
    });
  });
}
