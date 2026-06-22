// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionBlueprintLibrary.h"
#include "Cesium3DTileset.h"
#include "CesiumPointCloudCollisionManager.h"
#include "CesiumPointCloudCollisionProxy.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

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
