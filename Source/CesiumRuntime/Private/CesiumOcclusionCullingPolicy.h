// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

// Decides whether Cesium's experimental occlusion culling may be enabled for a
// tileset. Occlusion culling is DISABLED in game worlds (PIE and packaged
// games): on UE 5.7 the bounding-volume proxies never receive real
// occlusion-query results in game views, so every tile oscillates between
// synthesized-Occluded and OcclusionUnavailable and cesium-native's traversal
// pins the tileset at the root (forced Render + meetsSse each frame, children
// never visited) -- the tileset never loads past the root until something
// (e.g. selecting the actor in the editor) forces a NotOccluded result. This
// is the upstream cesium-unreal #1429 bug class; the v2.6.0 fix in
// CesiumViewExtension holds only for editor views, which do get real
// per-primitive occlusion queries. Disabling the feature in game worlds is the
// fail-open behavior cesium-native's TileOcclusionRendererProxy contract asks
// for (never report occlusion as indefinitely unavailable). Remove the game
// world exclusion if/when upstream restores working occlusion feedback for
// game views.
namespace CesiumOcclusionCullingPolicy {

inline bool ShouldEnable(
    bool bProjectFeatureEnabled,
    bool bTilesetOptionEnabled,
    bool bIsGameWorld) {
  return bProjectFeatureEnabled && bTilesetOptionEnabled && !bIsGameWorld;
}

} // namespace CesiumOcclusionCullingPolicy
