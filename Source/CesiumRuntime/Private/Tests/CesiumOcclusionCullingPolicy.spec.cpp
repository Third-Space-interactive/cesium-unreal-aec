// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumOcclusionCullingPolicy.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
    FCesiumOcclusionCullingPolicySpec,
    "Cesium.Unit.PointCloudCollision.OcclusionPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::ServerContext |
        EAutomationTestFlags::CommandletContext |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCesiumOcclusionCullingPolicySpec)

void FCesiumOcclusionCullingPolicySpec::Define() {
  // ACesium3DTileset::GetEnableOcclusionCulling() is a thin composition of this
  // pure policy with GetWorld()->IsGameWorld() and a project setting; the
  // composition itself is verified in-editor (PIE loads with the tileset
  // option enabled; editor viewport still culls).
  Describe("ShouldEnable", [this]() {
    It("is on in a non-game world when both flags are set", [this]() {
      TestTrue(
          TEXT("editor world, both flags"),
          CesiumOcclusionCullingPolicy::ShouldEnable(true, true, false));
    });
    It("is off in a game world even when both flags are set", [this]() {
      TestFalse(
          TEXT("game world blocks (PIE no-load latch)"),
          CesiumOcclusionCullingPolicy::ShouldEnable(true, true, true));
    });
    It("is off when the project feature flag is disabled", [this]() {
      TestFalse(
          TEXT("project feature off"),
          CesiumOcclusionCullingPolicy::ShouldEnable(false, true, false));
    });
    It("is off when the tileset option is disabled", [this]() {
      TestFalse(
          TEXT("tileset option off"),
          CesiumOcclusionCullingPolicy::ShouldEnable(true, false, false));
    });
  });
}
