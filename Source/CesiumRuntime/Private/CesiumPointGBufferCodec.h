// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"

// Bit layout of the per-pixel render record stored in the 64-bit depth atomic:
//   depth24 [63..40] | color24 [39..16] | bSynthNormal [15] | normalOct15 [14..0]
// Depth occupies the high bits so a single InterlockedMax keeps the nearest
// point's whole record (color + normal) atomically. The HLSL mirror in
// CesiumPointRaster.usf MUST match these constants and the octahedral math.
namespace CesiumPointGBufferCodec {

constexpr uint32 NormalBits = 15;
constexpr uint32 NormalMask = (1u << NormalBits) - 1u; // 0x7FFF
constexpr uint32 SynthShift = NormalBits;              // bit 15
constexpr uint32 ColorShift = 16;
constexpr uint32 ColorMask = 0xFFFFFFu; // 24-bit
constexpr uint32 DepthShift = 40;
constexpr uint32 DepthMask = 0xFFFFFFu; // 24-bit

// Octahedral wrap helper: folds the lower hemisphere onto the octahedron.
inline FVector2f OctWrap(const FVector2f& V) {
  return FVector2f(
      (1.0f - FMath::Abs(V.Y)) * (V.X >= 0.0f ? 1.0f : -1.0f),
      (1.0f - FMath::Abs(V.X)) * (V.Y >= 0.0f ? 1.0f : -1.0f));
}

inline uint16 EncodeNormalOct15(const FVector3f& N) {
  FVector3f n = N.GetSafeNormal();
  const float InvL1 =
      1.0f / (FMath::Abs(n.X) + FMath::Abs(n.Y) + FMath::Abs(n.Z));
  FVector2f p(n.X * InvL1, n.Y * InvL1);
  p = (n.Z < 0.0f) ? OctWrap(p) : p;
  // map [-1,1] -> 8 bits (X) and 7 bits (Y) = 15 bits total.
  const uint32 u = (uint32)FMath::Clamp(
      FMath::RoundToInt((p.X * 0.5f + 0.5f) * 255.0f),
      0,
      255); // 8 bits
  const uint32 v = (uint32)FMath::Clamp(
      FMath::RoundToInt((p.Y * 0.5f + 0.5f) * 127.0f),
      0,
      127); // 7 bits
  return (uint16)((u << 7) | v);
}

inline FVector3f DecodeNormalOct15(uint16 Oct) {
  const uint32 u = (Oct >> 7) & 0xFFu; // 8 bits
  const uint32 v = Oct & 0x7Fu;        // 7 bits
  FVector2f f((u / 255.0f) * 2.0f - 1.0f, (v / 127.0f) * 2.0f - 1.0f);
  FVector3f n(f.X, f.Y, 1.0f - FMath::Abs(f.X) - FMath::Abs(f.Y));
  const float t = FMath::Clamp(-n.Z, 0.0f, 1.0f);
  n.X += (n.X >= 0.0f) ? -t : t;
  n.Y += (n.Y >= 0.0f) ? -t : t;
  return n.GetSafeNormal();
}

inline uint64 PackRecord(
    uint32 Depth24,
    uint32 Color24,
    bool bSynthNormal,
    uint16 NormalOct15) {
  return ((uint64)(Depth24 & DepthMask) << DepthShift) |
         ((uint64)(Color24 & ColorMask) << ColorShift) |
         ((uint64)(bSynthNormal ? 1u : 0u) << SynthShift) |
         ((uint64)(NormalOct15 & NormalMask));
}

inline void UnpackRecord(
    uint64 Record,
    uint32& OutDepth24,
    uint32& OutColor24,
    bool& OutSynth,
    uint16& OutNormalOct15) {
  OutDepth24 = (uint32)((Record >> DepthShift) & DepthMask);
  OutColor24 = (uint32)((Record >> ColorShift) & ColorMask);
  OutSynth = ((Record >> SynthShift) & 1u) != 0u;
  OutNormalOct15 = (uint16)(Record & NormalMask);
}

} // namespace CesiumPointGBufferCodec
