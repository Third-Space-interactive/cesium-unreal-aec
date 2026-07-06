// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"

namespace CesiumPointComputeRaster {

// Pure decision: the compute path runs only when explicitly requested AND the
// RHI supports 64-bit atomics (required for depth-packed InterlockedMax/Min).
bool ShouldEnable(bool bRequested, bool bAtomicUInt64Supported);

// Live state: reads the r.Cesium.PointCloud.ComputeRaster CVar and the RHI
// capability global. Safe to call from game or render thread.
bool IsActive();

} // namespace CesiumPointComputeRaster
