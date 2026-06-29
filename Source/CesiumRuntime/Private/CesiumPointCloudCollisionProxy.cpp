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

void UCesiumPointCloudCollisionProxy::SetWorldBounds(const FBox& WorldBox) {
  if (!WorldBox.IsValid) {
    return;
  }
  // Enforce a minimum half-extent so near-planar tiles (a flat floor, or a
  // wall scanned head-on) still produce a non-degenerate, line-trace-hittable
  // collision box. A zero-thickness box is unreliable for physics queries.
  constexpr double MinHalfExtent = 1.0; // Unreal units (cm)
  const FVector Extent =
      WorldBox.GetExtent().ComponentMax(FVector(MinHalfExtent));
  SetWorldLocation(WorldBox.GetCenter());
  SetBoxExtent(Extent, true);
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
