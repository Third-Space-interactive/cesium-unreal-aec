// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointComputeRaster.h"
#include "CesiumRuntime.h"
#include "HAL/IConsoleManager.h"
#include "RHIGlobals.h"
#include <atomic>

namespace {
TAutoConsoleVariable<int32> CVarPointComputeRaster(
    TEXT("r.Cesium.PointCloud.ComputeRaster"),
    0,
    TEXT("Render Cesium point clouds via a compute rasterizer instead of per-tile ")
    TEXT("proxies. Requires 64-bit atomics; forced off if unsupported. Default 0."),
    ECVF_RenderThreadSafe);
} // namespace

namespace CesiumPointComputeRaster {

bool ShouldEnable(bool bRequested, bool bAtomicUInt64Supported) {
  return bRequested && bAtomicUInt64Supported;
}

bool IsActive() {
  const bool bRequested = CVarPointComputeRaster.GetValueOnAnyThread() != 0;
  if (bRequested && !GRHISupportsAtomicUInt64) {
    static std::atomic<bool> bWarned{false};
    if (!bWarned.exchange(true)) {
      UE_LOG(
          LogCesium,
          Warning,
          TEXT("r.Cesium.PointCloud.ComputeRaster requested but "
               "GRHISupportsAtomicUInt64 is false; staying on the proxy path."));
    }
  }
  return ShouldEnable(bRequested, GRHISupportsAtomicUInt64);
}

} // namespace CesiumPointComputeRaster
