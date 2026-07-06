// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"

// Packs a per-tile slot and a per-tile local point index into a single 32-bit
// payload that travels in the low half of the depth-packed 64-bit atomic. The
// HLSL side (CesiumPointRaster.usf) MUST mirror these constants exactly.
namespace CesiumPointIdCodec {

constexpr uint32 TileSlotBits = 8;
constexpr uint32 LocalIndexBits = 24;
constexpr uint32 MaxTileSlots = 1u << TileSlotBits;        // 256
constexpr uint32 MaxLocalIndex = (1u << LocalIndexBits) - 1; // 16,777,215

constexpr uint32 Encode(uint32 TileSlot, uint32 LocalIndex) {
  return (TileSlot << LocalIndexBits) | (LocalIndex & MaxLocalIndex);
}

constexpr uint32 DecodeTileSlot(uint32 GlobalId) {
  return GlobalId >> LocalIndexBits;
}

constexpr uint32 DecodeLocalIndex(uint32 GlobalId) {
  return GlobalId & MaxLocalIndex;
}

} // namespace CesiumPointIdCodec
