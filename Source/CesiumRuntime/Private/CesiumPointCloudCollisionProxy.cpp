// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#include "CesiumPointCloudCollisionProxy.h"

UCesiumPointCloudCollisionProxy::UCesiumPointCloudCollisionProxy() {
  SetHiddenInGame(true);
  SetVisibility(false);
  SetGenerateOverlapEvents(false);
  SetCanEverAffectNavigation(false);
  PrimaryComponentTick.bCanEverTick = false;

  // Default setup; reconfigured via ConfigureCollision().
  SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  SetCollisionResponseToAllChannels(ECR_Ignore);
  SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void UCesiumPointCloudCollisionProxy::ConfigureCollision(
    ECollisionChannel TraceChannel) {
  SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  SetCollisionResponseToAllChannels(ECR_Ignore);
  SetCollisionResponseToChannel(TraceChannel, ECR_Block);
}

void UCesiumPointCloudCollisionProxy::UpdateBoundsFromTileset() {
  AActor* Owner = GetOwner();
  if (!Owner) {
    return;
  }

  // Aggregate sibling primitive bounds, excluding self to avoid recursion.
  FBox ActorBounds(ForceInit);
  TArray<UPrimitiveComponent*> PrimitiveComponents;
  Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

  for (UPrimitiveComponent* Comp : PrimitiveComponents) {
    if (Comp == this || !Comp->IsRegistered()) {
      continue;
    }
    ActorBounds += Comp->Bounds.GetBox();
  }

  if (!ActorBounds.IsValid) {
    return;
  }

  SetWorldLocation(ActorBounds.GetCenter());
  SetBoxExtent(ActorBounds.GetExtent(), true);
}
