// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumPointIdCodec.h"
#include "CoreMinimal.h"
#include "RHIResources.h"

// One resident point tile's GPU resources, as seen by the compute raster pass.
// Populated on the render thread from the point scene proxy.
struct FCesiumPointTileEntry {
  FRHIShaderResourceView* PositionSRV = nullptr; // float, 3 floats/point
  FRHIShaderResourceView* ColorSRV = nullptr;    // float4/point, may be null
  FRHIShaderResourceView* TangentsSRV = nullptr; // packed tangents, 2 float4/point
  uint32 NumPoints = 0;
  FMatrix44f LocalToWorld = FMatrix44f::Identity;
  uint32 bHasColors = 0;
  uint32 bHasRealNormals = 0; // 1 if the glTF had a real NORMAL accessor
  int32 TileSlot = INDEX_NONE;
  bool bActive = false;
};

// Render-thread-only registry of live point proxies. Fixed-size free list so a
// TileSlot is stable for an entry's lifetime and indexes the GPU tile-params
// array directly. *ForTesting variants skip the render-thread assert.
class FCesiumPointProxyRegistry {
public:
  // Max resident point tiles the compute raster can render at once. Decoupled
  // from CesiumPointIdCodec::MaxTileSlots (the 8-bit pick-ID limit): the lit
  // render record carries no tile slot, so render capacity is independent of
  // picking. Sized well above the observed dense-campus resident count (~1,800).
  // NOTE: the future GPU pick path (Task 7) still only addresses the first 256
  // tiles until its payload is widened.
  static constexpr int32 MaxResidentTiles = 8192;

  static FCesiumPointProxyRegistry& Get();
  static FCesiumPointProxyRegistry MakeForTesting() {
    return FCesiumPointProxyRegistry();
  }

  int32 Register(const FCesiumPointTileEntry& Entry);
  void Unregister(int32 TileSlot);
  void UpdateTransform(int32 TileSlot, const FMatrix44f& LocalToWorld);
  TArray<FCesiumPointTileEntry> SnapshotEntries() const;
  bool DecodeComponentLocal(
      uint32 GlobalId,
      int32& OutTileSlot,
      uint32& OutLocalIndex) const;

  // Test seams (no render-thread assert).
  int32 RegisterForTesting(const FCesiumPointTileEntry& Entry) {
    return RegisterInternal(Entry);
  }
  void UnregisterForTesting(int32 TileSlot) { UnregisterInternal(TileSlot); }
  TArray<FCesiumPointTileEntry> SnapshotEntriesForTesting() const {
    return SnapshotInternal();
  }
  bool DecodeComponentLocalForTesting(
      uint32 GlobalId,
      int32& OutTileSlot,
      uint32& OutLocalIndex) const {
    return DecodeInternal(GlobalId, OutTileSlot, OutLocalIndex);
  }

private:
  int32 RegisterInternal(const FCesiumPointTileEntry& Entry);
  void UnregisterInternal(int32 TileSlot);
  TArray<FCesiumPointTileEntry> SnapshotInternal() const;
  bool DecodeInternal(
      uint32 GlobalId,
      int32& OutTileSlot,
      uint32& OutLocalIndex) const;

  TStaticArray<FCesiumPointTileEntry, MaxResidentTiles> Entries;
};
