// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#include "CesiumPointComputeRaster.h"
#include "CesiumPointGBufferCodec.h"
#include "CesiumPointIdCodec.h"
#include "CesiumPointProxyRegistry.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
    FCesiumPointComputeRasterSpec,
    "Cesium.Unit.PointCloudCollision.ComputeRaster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::ServerContext |
        EAutomationTestFlags::CommandletContext |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCesiumPointComputeRasterSpec)

void FCesiumPointComputeRasterSpec::Define() {
  // IsActive() is intentionally not unit-tested here. It is a thin composition
  // of the pure, unit-tested ShouldEnable() with two engine globals
  // (the r.Cesium.PointCloud.ComputeRaster CVar and GRHISupportsAtomicUInt64)
  // that cannot be set deterministically in a -nullrhi headless run. Adding a
  // test that mutates those globals would be brittle and environment-dependent.
  Describe("ShouldEnable", [this]() {
    It("is off when not requested", [this]() {
      TestFalse(TEXT("off"), CesiumPointComputeRaster::ShouldEnable(false, true));
    });
    It("is off when atomics unsupported even if requested", [this]() {
      TestFalse(TEXT("off"), CesiumPointComputeRaster::ShouldEnable(true, false));
    });
    It("is on when requested and atomics supported", [this]() {
      TestTrue(TEXT("on"), CesiumPointComputeRaster::ShouldEnable(true, true));
    });
  });
  Describe("PointIdCodec", [this]() {
    It("round-trips slot and local index", [this]() {
      const uint32 Id = CesiumPointIdCodec::Encode(5, 123456);
      TestEqual(TEXT("slot"), CesiumPointIdCodec::DecodeTileSlot(Id), 5u);
      TestEqual(TEXT("local"), CesiumPointIdCodec::DecodeLocalIndex(Id), 123456u);
    });
    It("round-trips boundary values", [this]() {
      const uint32 Id = CesiumPointIdCodec::Encode(
          CesiumPointIdCodec::MaxTileSlots - 1,
          CesiumPointIdCodec::MaxLocalIndex);
      TestEqual(
          TEXT("slot"),
          CesiumPointIdCodec::DecodeTileSlot(Id),
          (uint32)(CesiumPointIdCodec::MaxTileSlots - 1));
      TestEqual(
          TEXT("local"),
          CesiumPointIdCodec::DecodeLocalIndex(Id),
          (uint32)CesiumPointIdCodec::MaxLocalIndex);
    });
  });
  Describe("ProxyRegistry", [this]() {
    It("assigns distinct slots and snapshots active entries", [this]() {
      FCesiumPointProxyRegistry Reg = FCesiumPointProxyRegistry::MakeForTesting();
      FCesiumPointTileEntry A{};
      A.NumPoints = 10;
      FCesiumPointTileEntry B{};
      B.NumPoints = 20;
      const int32 SlotA = Reg.RegisterForTesting(A);
      const int32 SlotB = Reg.RegisterForTesting(B);
      TestNotEqual(TEXT("distinct slots"), SlotA, SlotB);
      TestEqual(TEXT("two active"), Reg.SnapshotEntriesForTesting().Num(), 2);
    });
    It("frees a slot on unregister and reuses it", [this]() {
      FCesiumPointProxyRegistry Reg = FCesiumPointProxyRegistry::MakeForTesting();
      FCesiumPointTileEntry A{};
      const int32 SlotA = Reg.RegisterForTesting(A);
      Reg.UnregisterForTesting(SlotA);
      TestEqual(TEXT("none active"), Reg.SnapshotEntriesForTesting().Num(), 0);
      const int32 SlotReused = Reg.RegisterForTesting(A);
      TestEqual(TEXT("slot reused"), SlotReused, SlotA);
    });
    It("decodes a global id back to its slot and local index", [this]() {
      FCesiumPointProxyRegistry Reg = FCesiumPointProxyRegistry::MakeForTesting();
      FCesiumPointTileEntry A{};
      A.NumPoints = 500;
      const int32 Slot = Reg.RegisterForTesting(A);
      const uint32 Gid = CesiumPointIdCodec::Encode((uint32)Slot, 321);
      int32 OutSlot = INDEX_NONE;
      uint32 OutLocal = 0;
      TestTrue(TEXT("decoded"), Reg.DecodeComponentLocalForTesting(Gid, OutSlot, OutLocal));
      TestEqual(TEXT("slot"), OutSlot, Slot);
      TestEqual(TEXT("local"), OutLocal, 321u);
    });
  });
  Describe("GBufferCodec", [this]() {
    It("packs and unpacks a record losslessly", [this]() {
      const uint32 Depth = 0x00ABCDEFu & 0xFFFFFFu; // 24-bit
      const uint32 Color = 0x00123456u & 0xFFFFFFu; // 24-bit
      const uint16 Oct = 0x2ABCu & 0x7FFFu;         // 15-bit
      const uint64 Rec =
          CesiumPointGBufferCodec::PackRecord(Depth, Color, true, Oct);
      uint32 OutDepth = 0, OutColor = 0;
      bool OutSynth = false;
      uint16 OutOct = 0;
      CesiumPointGBufferCodec::UnpackRecord(
          Rec,
          OutDepth,
          OutColor,
          OutSynth,
          OutOct);
      TestEqual(TEXT("depth"), OutDepth, Depth);
      TestEqual(TEXT("color"), OutColor, Color);
      TestTrue(TEXT("synth"), OutSynth);
      TestEqual(TEXT("oct"), (uint32)OutOct, (uint32)Oct);
    });
    It("orders by depth in the high bits (nearest-wins under max)", [this]() {
      const uint64 Near =
          CesiumPointGBufferCodec::PackRecord(0xFFFFFFu, 0u, false, 0u);
      const uint64 Far =
          CesiumPointGBufferCodec::PackRecord(0x000001u, 0xFFFFFFu, true, 0x7FFFu);
      TestTrue(TEXT("near record is larger"), Near > Far);
    });
    It("keeps the synth flag independent of the normal field", [this]() {
      const uint64 Rec =
          CesiumPointGBufferCodec::PackRecord(0u, 0u, false, 0x7FFFu);
      uint32 D = 0, C = 0;
      bool S = true;
      uint16 O = 0;
      CesiumPointGBufferCodec::UnpackRecord(Rec, D, C, S, O);
      TestFalse(TEXT("flag is false"), S);
      TestEqual(TEXT("oct preserved"), (uint32)O, 0x7FFFu);
    });
    It("round-trips axis normals through octahedral encoding", [this]() {
      const FVector3f Axes[] = {
          FVector3f(0, 0, 1),
          FVector3f(0, 0, -1),
          FVector3f(1, 0, 0),
          FVector3f(-1, 0, 0),
          FVector3f(0, 1, 0),
          FVector3f(0, -1, 0)};
      for (const FVector3f& N : Axes) {
        const uint16 Oct = CesiumPointGBufferCodec::EncodeNormalOct15(N);
        const FVector3f D = CesiumPointGBufferCodec::DecodeNormalOct15(Oct);
        TestTrue(
            FString::Printf(TEXT("dot>0.99 for %s"), *N.ToString()),
            FVector3f::DotProduct(N, D) > 0.99f);
      }
    });
  });
  Describe("RegistryNormals", [this]() {
    It("stores tangents SRV pointer and bHasRealNormals on the entry", [this]() {
      FCesiumPointProxyRegistry Reg = FCesiumPointProxyRegistry::MakeForTesting();
      FCesiumPointTileEntry A{};
      A.NumPoints = 7;
      A.bHasRealNormals = 1u;
      // A non-null sentinel pointer is enough to prove the field round-trips.
      A.TangentsSRV = reinterpret_cast<FRHIShaderResourceView*>(0x1);
      const int32 Slot = Reg.RegisterForTesting(A);
      const TArray<FCesiumPointTileEntry> Snap = Reg.SnapshotEntriesForTesting();
      TestEqual(TEXT("one entry"), Snap.Num(), 1);
      TestEqual(TEXT("slot"), Snap[0].TileSlot, Slot);
      TestEqual(TEXT("hasRealNormals"), Snap[0].bHasRealNormals, 1u);
      TestTrue(TEXT("tangents kept"), Snap[0].TangentsSRV != nullptr);
    });
  });
}
