// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionBlueprintLibrary.h"
#include "Cesium3DTileset.h"
#include "CesiumPointCloudCollisionManager.h"
#include "CesiumPointCloudCollisionProxy.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

void UCesiumPointCloudCollisionBlueprintLibrary::
    AddCollisionManagerToAllTilesets(const UObject* WorldContextObject) {
  UWorld* World =
      GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
  if (!World) {
    return;
  }

  for (TActorIterator<ACesium3DTileset> It(World); It; ++It) {
    ACesium3DTileset* Tileset = *It;
    UCesiumPointCloudCollisionManager* Existing =
        Tileset->FindComponentByClass<UCesiumPointCloudCollisionManager>();
    if (!Existing) {
      UCesiumPointCloudCollisionManager* Manager =
          NewObject<UCesiumPointCloudCollisionManager>(
              Tileset,
              UCesiumPointCloudCollisionManager::StaticClass(),
              TEXT("PointCloudCollisionManager"));
      Manager->RegisterComponent();
      Tileset->AddInstanceComponent(Manager);
    }
  }
}

void UCesiumPointCloudCollisionBlueprintLibrary::
    RemoveCollisionManagerFromAllTilesets(
        const UObject* WorldContextObject) {
  UWorld* World =
      GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
  if (!World) {
    return;
  }

  for (TActorIterator<ACesium3DTileset> It(World); It; ++It) {
    ACesium3DTileset* Tileset = *It;
    UCesiumPointCloudCollisionManager* Manager =
        Tileset->FindComponentByClass<UCesiumPointCloudCollisionManager>();
    if (Manager) {
      Manager->DestroyCollisionProxy();
      Manager->DestroyComponent();
    }
  }
}

void UCesiumPointCloudCollisionBlueprintLibrary::
    SetCollisionChannelForAllManagers(
        const UObject* WorldContextObject,
        ECollisionChannel Channel) {
  UWorld* World =
      GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
  if (!World) {
    return;
  }

  for (TActorIterator<ACesium3DTileset> It(World); It; ++It) {
    ACesium3DTileset* Tileset = *It;
    UCesiumPointCloudCollisionManager* Manager =
        Tileset->FindComponentByClass<UCesiumPointCloudCollisionManager>();
    if (Manager) {
      Manager->TraceChannel = Channel;
      UCesiumPointCloudCollisionProxy* Proxy = Manager->GetCollisionProxy();
      if (Proxy) {
        Proxy->ConfigureCollision(Channel);
      }
    }
  }
}

FCesiumPointCloudHit
UCesiumPointCloudCollisionBlueprintLibrary::QueryPointCloudAlongRay(
    UObject* WorldContextObject,
    FVector Origin,
    FVector Direction,
    float MaxDistance,
    ECollisionChannel TraceChannel) {
  FCesiumPointCloudHit Best;
  if (!WorldContextObject) {
    return Best;
  }
  UWorld* World = WorldContextObject->GetWorld();
  if (!World) {
    return Best;
  }

  const FVector Dir = Direction.GetSafeNormal();
  const FVector End = Origin + Dir * MaxDistance;

  // Tier-1 broadphase: all tileset boxes the ray crosses.
  TArray<FHitResult> Hits;
  FCollisionQueryParams Params(SCENE_QUERY_STAT(PointCloudBroadphase), true);
  World->LineTraceMultiByChannel(Hits, Origin, End, TraceChannel, Params);

  double BestDist = MaxDistance;
  for (const FHitResult& H : Hits) {
    AActor* Actor = H.GetActor();
    if (!Actor) {
      continue;
    }
    UCesiumPointCloudCollisionManager* Mgr =
        Actor->FindComponentByClass<UCesiumPointCloudCollisionManager>();
    if (!Mgr) {
      continue;
    }

    if (!Mgr->bEnablePreciseRefine) {
      // Phase 1 fallback: use the box hit itself.
      const double WDist = FVector::Dist(Origin, H.ImpactPoint);
      if (WDist < BestDist) {
        BestDist = WDist;
        Best.bHit = true;
        Best.Tileset = Cast<ACesium3DTileset>(Actor);
        Best.WorldPoint = H.ImpactPoint;
        Best.WorldNormal = H.ImpactNormal;
        Best.Distance = (float)WDist;
      }
      continue;
    }

    FCesiumPointCloudHit Refined = Mgr->RefineRay(Origin, Dir, MaxDistance);
    if (Refined.bHit && Refined.Distance < BestDist) {
      BestDist = Refined.Distance;
      Best = Refined;
    }
  }

  return Best;
}

FCesiumPointCloudHit
UCesiumPointCloudCollisionBlueprintLibrary::QueryPointCloudUnderCursor(
    APlayerController* PlayerController,
    float MaxDistance) {
  FCesiumPointCloudHit Best;
  if (!PlayerController) {
    return Best;
  }
  FVector Origin, Dir;
  if (!PlayerController->DeprojectMousePositionToWorld(Origin, Dir)) {
    return Best;
  }
  return QueryPointCloudAlongRay(
      PlayerController,
      Origin,
      Dir,
      MaxDistance,
      ECC_Visibility);
}
