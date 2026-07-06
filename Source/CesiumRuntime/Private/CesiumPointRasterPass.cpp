// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointRasterPass.h"
#include "CesiumPointComputeRaster.h"
#include "CesiumPointProxyRegistry.h"
#include "CesiumRuntime.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

static const FViewInfo* AsViewInfo(const FSceneView& View) {
  return View.bIsViewInfo ? static_cast<const FViewInfo*>(&View) : nullptr;
}

static constexpr int32 kPointRasterThreadsPerGroup = 64; // must match RasterPointsCS/ClearBufferCS numthreads

namespace {

TAutoConsoleVariable<float> CVarPointRoughness(
    TEXT("r.Cesium.PointCloud.ComputeRaster.Roughness"),
    1.0f,
    TEXT("GBuffer roughness for compute-rasterized lit points. Default 1.0."),
    ECVF_RenderThreadSafe);

// All shaders require SM6.6+ 64-bit atomics (UlongType in HLSL). Gate the
// permutation on the same RHI capability the runtime gate (IsActive) checks.
static bool CesiumPointRasterShouldCompile(
    const FGlobalShaderPermutationParameters& P) {
  return FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(
      P.Platform);
}

class FCesiumClearBufferCS : public FGlobalShader {
public:
  DECLARE_GLOBAL_SHADER(FCesiumClearBufferCS);
  SHADER_USE_PARAMETER_STRUCT(FCesiumClearBufferCS, FGlobalShader);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
  SHADER_PARAMETER(FUintVector2, ViewportSize)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<UlongType>, DepthPayloadBuffer)
  END_SHADER_PARAMETER_STRUCT()
  static bool ShouldCompilePermutation(
      const FGlobalShaderPermutationParameters& P) {
    return CesiumPointRasterShouldCompile(P);
  }
};

class FCesiumRasterPointsCS : public FGlobalShader {
public:
  DECLARE_GLOBAL_SHADER(FCesiumRasterPointsCS);
  SHADER_USE_PARAMETER_STRUCT(FCesiumRasterPointsCS, FGlobalShader);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
  SHADER_PARAMETER_SRV(Buffer<float>, PositionBuffer)
  SHADER_PARAMETER_SRV(Buffer<float4>, ColorBuffer)
  SHADER_PARAMETER_SRV(Buffer<float4>, TangentsBuffer)
  SHADER_PARAMETER(uint32, NumPoints)
  SHADER_PARAMETER(uint32, bHasColors)
  SHADER_PARAMETER(uint32, bHasRealNormals)
  SHADER_PARAMETER(FMatrix44f, LocalToClip)
  SHADER_PARAMETER(FMatrix44f, LocalToWorldNormal)
  SHADER_PARAMETER(FUintVector2, ViewportSize)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<UlongType>, DepthPayloadBuffer)
  END_SHADER_PARAMETER_STRUCT()
  static bool ShouldCompilePermutation(
      const FGlobalShaderPermutationParameters& P) {
    return CesiumPointRasterShouldCompile(P);
  }
};

class FCesiumResolveGBufferPS : public FGlobalShader {
public:
  DECLARE_GLOBAL_SHADER(FCesiumResolveGBufferPS);
  SHADER_USE_PARAMETER_STRUCT(FCesiumResolveGBufferPS, FGlobalShader);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
  SHADER_PARAMETER(FUintVector2, ViewportSize)
  SHADER_PARAMETER(FUintVector2, ViewRectMin)
  SHADER_PARAMETER(float, PointRoughness)
  SHADER_PARAMETER(float, PointSpecular)
  SHADER_PARAMETER(float, PointMetallic)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<UlongType>, DepthPayloadBufferRead)
  RENDER_TARGET_BINDING_SLOTS()
  END_SHADER_PARAMETER_STRUCT()
  static bool ShouldCompilePermutation(
      const FGlobalShaderPermutationParameters& P) {
    return CesiumPointRasterShouldCompile(P);
  }
};

// Pass parameters for the fused multi-dispatch raster pass. The DepthPayload
// UAV is the ONLY RDG resource the raster phase touches — declaring it once
// here (instead of once per per-tile pass) collapses the raster phase to a
// single RDG pass with one barrier on each side (clear->raster and
// raster->resolve are preserved; the ~1,799 inter-dispatch UAV barriers are
// not, which is correct: InterlockedMax writes are order-independent).
// Per-tile vertex SRVs are raw RHI views (never RDG-tracked), bound per
// dispatch inside the pass lambda.
BEGIN_SHADER_PARAMETER_STRUCT(FCesiumRasterAllPassParameters, )
SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<UlongType>, DepthPayloadBuffer)
END_SHADER_PARAMETER_STRUCT()

} // namespace

IMPLEMENT_GLOBAL_SHADER(
    FCesiumClearBufferCS,
    "/Plugin/CesiumForUnreal/Private/CesiumPointRaster.usf",
    "ClearBufferCS",
    SF_Compute);
IMPLEMENT_GLOBAL_SHADER(
    FCesiumRasterPointsCS,
    "/Plugin/CesiumForUnreal/Private/CesiumPointRaster.usf",
    "RasterPointsCS",
    SF_Compute);
IMPLEMENT_GLOBAL_SHADER(
    FCesiumResolveGBufferPS,
    "/Plugin/CesiumForUnreal/Private/CesiumPointRaster.usf",
    "ResolveGBufferPS",
    SF_Pixel);

namespace CesiumPointRasterPass {

void AddRenderPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FRenderTargetBindingSlots& RenderTargets,
    TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) {
  if (!CesiumPointComputeRaster::IsActive()) {
    return;
  }

  // Unified atomic capability gate (review nit #1): the runtime buffer-atomic
  // flag AND the shader-permutation image-atomic capability.
  if (!GRHISupportsAtomicUInt64 ||
      !FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(
          GShaderPlatformForFeatureLevel[View.FeatureLevel])) {
    static bool bLoggedAtomics = false;
    if (!bLoggedAtomics) {
      bLoggedAtomics = true;
      UE_LOG(
          LogCesium,
          Warning,
          TEXT("Cesium point compute raster: active but skipped — 64-bit "
               "atomics unsupported (buffer=%d, imageOnPlatform=%d, "
               "featureLevel=%d). Points will not render while the CVar is "
               "on."),
          GRHISupportsAtomicUInt64 ? 1 : 0,
          FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(
              GShaderPlatformForFeatureLevel[View.FeatureLevel])
              ? 1
              : 0,
          (int32)View.FeatureLevel);
    }
    return;
  }

  // The GBuffer + depth-stencil must be bound (deferred renderer). Bail if the
  // depth target is missing rather than tripping an RDG assert.
  if (RenderTargets.DepthStencil.GetTexture() == nullptr) {
    static bool bLoggedNoDepth = false;
    if (!bLoggedNoDepth) {
      bLoggedNoDepth = true;
      UE_LOG(
          LogCesium,
          Warning,
          TEXT("Cesium point compute raster: active but skipped — no bound "
               "depth-stencil target at the base-pass hook."));
    }
    return;
  }

  TArray<FCesiumPointTileEntry> Tiles =
      FCesiumPointProxyRegistry::Get().SnapshotEntries();
  if (Tiles.Num() == 0) {
    static bool bLoggedNoTiles = false;
    if (!bLoggedNoTiles) {
      bLoggedNoTiles = true;
      UE_LOG(
          LogCesium,
          Display,
          TEXT("Cesium point compute raster: active with 0 registered tiles "
               "(expected at startup; a later 'pass running' log confirms "
               "recovery)."));
    }
    return;
  }

  // Use the internal render-res rect (FViewInfo::ViewRect), not the display rect
  // (UnscaledViewRect) — they differ under screen-percentage / TSR upscaling.
  // Non-FViewInfo paths (thumbnails/scene-captures) fall back to the display
  // rect, which may misalign under screen% — acceptable; the path targets main
  // views.
  const FViewInfo* ViewInfo = AsViewInfo(View);
  FIntRect ViewRect = ViewInfo ? ViewInfo->ViewRect : View.UnscaledViewRect;
  const FIntPoint Extent = ViewRect.Size();
  if (Extent.X <= 0 || Extent.Y <= 0) {
    return;
  }
  const uint32 NumPixels = (uint32)(Extent.X * Extent.Y);
  const FUintVector2 ViewportSize((uint32)Extent.X, (uint32)Extent.Y);

  // 64-bit depth+payload buffer (one element per pixel).
  FRDGBufferRef DepthPayload = GraphBuilder.CreateBuffer(
      FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64), NumPixels),
      TEXT("CesiumPointDepthPayload"));
  FRDGBufferUAVRef DepthPayloadUAV = GraphBuilder.CreateUAV(DepthPayload);

  FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);

  // Clear to 0 (farthest under reversed-Z).
  {
    FCesiumClearBufferCS::FParameters* P =
        GraphBuilder.AllocParameters<FCesiumClearBufferCS::FParameters>();
    P->ViewportSize = ViewportSize;
    P->DepthPayloadBuffer = DepthPayloadUAV;
    TShaderMapRef<FCesiumClearBufferCS> CS(ShaderMap);
    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("CesiumPointClear"),
        CS,
        P,
        FComputeShaderUtils::GetGroupCount(
            (int32)NumPixels,
            kPointRasterThreadsPerGroup));
  }

  // Build LocalToClip per tile: LocalToWorld -> translated world -> clip.
  // Points are authored in tile-local space; LocalToWorld maps local->world.
  const FMatrix44f TranslatedWorldToClip =
      FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());
  const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();

  // Pre-build the per-tile dispatch list at graph-build time, then execute
  // every dispatch inside ONE RDG pass (see FCesiumRasterAllPassParameters).
  struct FTileDispatch {
    FCesiumRasterPointsCS::FParameters* Parameters = nullptr;
    FIntVector GroupCount = FIntVector(0, 0, 0);
  };
  TArray<FTileDispatch>& TileDispatches =
      *GraphBuilder.AllocObject<TArray<FTileDispatch>>();
  TileDispatches.Reserve(Tiles.Num());
  uint32 TotalPoints = 0;

  for (const FCesiumPointTileEntry& Tile : Tiles) {
    if (Tile.PositionSRV == nullptr || Tile.NumPoints == 0) {
      continue;
    }
    // No engine float4 SRV stub; skip colorless tiles this increment.
    if (Tile.ColorSRV == nullptr) {
      continue;
    }
    // LocalToWorld (float) -> translated world -> clip.
    FMatrix44f LocalToTranslatedWorld =
        Tile.LocalToWorld.ConcatTranslation(FVector3f(PreViewTranslation));
    const FMatrix44f LocalToClip =
        LocalToTranslatedWorld * TranslatedWorldToClip;

    FCesiumRasterPointsCS::FParameters* P =
        GraphBuilder.AllocParameters<FCesiumRasterPointsCS::FParameters>();
    P->PositionBuffer = Tile.PositionSRV;
    P->ColorBuffer = Tile.ColorSRV;
    P->TangentsBuffer = Tile.TangentsSRV;
    P->NumPoints = Tile.NumPoints;
    P->bHasColors = Tile.bHasColors;
    // No real-normal decode if the tangents SRV is missing.
    P->bHasRealNormals =
        (Tile.TangentsSRV != nullptr) ? Tile.bHasRealNormals : 0u;
    P->LocalToClip = LocalToClip;
    // Normals use the local->world upper-3x3 (translation dropped in HLSL).
    P->LocalToWorldNormal = Tile.LocalToWorld;
    P->ViewportSize = ViewportSize;
    P->DepthPayloadBuffer = DepthPayloadUAV;

    FTileDispatch Dispatch;
    Dispatch.Parameters = P;
    Dispatch.GroupCount = FComputeShaderUtils::GetGroupCount(
        (int32)Tile.NumPoints,
        kPointRasterThreadsPerGroup);
    TileDispatches.Add(Dispatch);
    TotalPoints += Tile.NumPoints;
  }

  if (TileDispatches.Num() > 0) {
    // One-shot diagnostic: the compute raster is genuinely rendering points in
    // this process (the early-outs above all log their own skip reasons).
    static bool bLoggedRunning = false;
    if (!bLoggedRunning) {
      bLoggedRunning = true;
      UE_LOG(
          LogCesium,
          Display,
          TEXT("Cesium point compute raster: pass running (%d tiles, %u "
               "points, viewport %ux%u)."),
          TileDispatches.Num(),
          TotalPoints,
          ViewportSize.X,
          ViewportSize.Y);
    }
    FCesiumRasterAllPassParameters* PassParameters =
        GraphBuilder.AllocParameters<FCesiumRasterAllPassParameters>();
    PassParameters->DepthPayloadBuffer = DepthPayloadUAV;

    TShaderMapRef<FCesiumRasterPointsCS> ComputeShader(ShaderMap);
    GraphBuilder.AddPass(
        RDG_EVENT_NAME(
            "CesiumPointRasterAll(%d tiles, %u pts)",
            TileDispatches.Num(),
            TotalPoints),
        PassParameters,
        ERDGPassFlags::Compute,
        [&TileDispatches, ComputeShader](FRHIComputeCommandList& RHICmdList) {
          for (const FTileDispatch& Dispatch : TileDispatches) {
            FComputeShaderUtils::Dispatch(
                RHICmdList,
                ComputeShader,
                *Dispatch.Parameters,
                Dispatch.GroupCount);
          }
        });
  }

  // Resolve: fullscreen pixel shader writes GBuffer MRTs + SV_Depth.
  {
    FCesiumResolveGBufferPS::FParameters* P =
        GraphBuilder.AllocParameters<FCesiumResolveGBufferPS::FParameters>();
    P->ViewportSize = ViewportSize;
    P->ViewRectMin =
        FUintVector2((uint32)ViewRect.Min.X, (uint32)ViewRect.Min.Y);
    P->PointRoughness = CVarPointRoughness.GetValueOnRenderThread();
    P->PointSpecular = 0.5f;
    P->PointMetallic = 0.0f;
    P->DepthPayloadBufferRead = GraphBuilder.CreateSRV(DepthPayload);
    // Write the same GBuffer MRTs the base pass bound; rebind depth as writable
    // so the hardware depth test occludes vs scene geometry (reversed-Z) AND we
    // write depth for TAA / the later sky pass.
    P->RenderTargets = RenderTargets;
    P->RenderTargets.DepthStencil = FDepthStencilBinding(
        RenderTargets.DepthStencil.GetTexture(),
        ERenderTargetLoadAction::ELoad,
        ERenderTargetLoadAction::ELoad,
        FExclusiveDepthStencil::DepthWrite_StencilNop);

    TShaderMapRef<FCesiumResolveGBufferPS> PS(ShaderMap);
    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        ShaderMap,
        RDG_EVENT_NAME("CesiumPointResolveGBuffer"),
        PS,
        P,
        FIntRect(ViewRect.Min, ViewRect.Max),
        /*BlendState*/ nullptr,
        /*RasterizerState*/ nullptr,
        /*DepthStencilState*/
        TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI());
  }
}

} // namespace CesiumPointRasterPass
