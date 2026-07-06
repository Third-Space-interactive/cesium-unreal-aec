// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointProxyRegistry.h"
#include "CesiumRuntime.h"
#include "RenderingThread.h"

FCesiumPointProxyRegistry& FCesiumPointProxyRegistry::Get() {
  static FCesiumPointProxyRegistry Instance;
  return Instance;
}

int32 FCesiumPointProxyRegistry::RegisterInternal(
    const FCesiumPointTileEntry& Entry) {
  for (int32 Slot = 0; Slot < Entries.Num(); ++Slot) {
    if (!Entries[Slot].bActive) {
      Entries[Slot] = Entry;
      Entries[Slot].TileSlot = Slot;
      Entries[Slot].bActive = true;
      // One-shot diagnostic: proves point tiles reach the registry in this
      // process (editor vs packaged triage for the compute raster path).
      static bool bLoggedFirst = false;
      if (!bLoggedFirst) {
        bLoggedFirst = true;
        UE_LOG(
            LogCesium,
            Display,
            TEXT("Cesium point compute raster: first point tile registered "
                 "(slot %d, %u points, colors=%u)"),
            Slot,
            Entry.NumPoints,
            Entry.bHasColors);
      }
      return Slot;
    }
  }
  static bool bWarnedFull = false;
  if (!bWarnedFull) {
    bWarnedFull = true;
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("Cesium point compute raster: resident point tiles exceeded "
             "MaxResidentTiles (%d); overflow tiles are not rendered. Raise "
             "FCesiumPointProxyRegistry::MaxResidentTiles."),
        Entries.Num());
  }
  return INDEX_NONE; // registry full (>= MaxResidentTiles resident point tiles)
}

void FCesiumPointProxyRegistry::UnregisterInternal(int32 TileSlot) {
  if (TileSlot >= 0 && TileSlot < Entries.Num()) {
    Entries[TileSlot] = FCesiumPointTileEntry{};
  }
}

TArray<FCesiumPointTileEntry>
FCesiumPointProxyRegistry::SnapshotInternal() const {
  TArray<FCesiumPointTileEntry> Out;
  for (const FCesiumPointTileEntry& E : Entries) {
    if (E.bActive) {
      Out.Add(E);
    }
  }
  return Out;
}

int32 FCesiumPointProxyRegistry::Register(const FCesiumPointTileEntry& Entry) {
  check(IsInRenderingThread());
  return RegisterInternal(Entry);
}

void FCesiumPointProxyRegistry::Unregister(int32 TileSlot) {
  check(IsInRenderingThread());
  UnregisterInternal(TileSlot);
}

void FCesiumPointProxyRegistry::UpdateTransform(
    int32 TileSlot,
    const FMatrix44f& LocalToWorld) {
  check(IsInRenderingThread());
  if (TileSlot >= 0 && TileSlot < Entries.Num() && Entries[TileSlot].bActive) {
    Entries[TileSlot].LocalToWorld = LocalToWorld;
  }
}

TArray<FCesiumPointTileEntry>
FCesiumPointProxyRegistry::SnapshotEntries() const {
  check(IsInRenderingThread());
  return SnapshotInternal();
}

bool FCesiumPointProxyRegistry::DecodeInternal(
    uint32 GlobalId,
    int32& OutTileSlot,
    uint32& OutLocalIndex) const {
  const uint32 Slot = CesiumPointIdCodec::DecodeTileSlot(GlobalId);
  if ((int32)Slot >= Entries.Num() || !Entries[Slot].bActive) {
    return false;
  }
  OutTileSlot = (int32)Slot;
  OutLocalIndex = CesiumPointIdCodec::DecodeLocalIndex(GlobalId);
  return true;
}

bool FCesiumPointProxyRegistry::DecodeComponentLocal(
    uint32 GlobalId,
    int32& OutTileSlot,
    uint32& OutLocalIndex) const {
  check(IsInRenderingThread());
  return DecodeInternal(GlobalId, OutTileSlot, OutLocalIndex);
}
