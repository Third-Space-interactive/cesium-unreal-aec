// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "SceneTexturesConfig.h"

class FSceneView;
struct FRenderTargetBindingSlots;

namespace CesiumPointRasterPass {

// Rasterizes all resident point tiles into the GBuffer (base color + normal) and
// the depth buffer at the pre-lighting hook (PostRenderBasePassDeferred), so UE
// deferred lighting lights them and TAA gets correct depth. No-op unless
// CesiumPointComputeRaster::IsActive(). RenderTargets carries the base-pass
// GBuffer MRTs + depth-stencil to write into.
void AddRenderPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FRenderTargetBindingSlots& RenderTargets,
    TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures);

} // namespace CesiumPointRasterPass
