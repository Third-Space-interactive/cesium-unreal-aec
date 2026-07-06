// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumCompat.h"
#include "CesiumPointAttenuationVertexFactory.h"
#include "CesiumPointCloudShading.h"
#include "PrimitiveSceneProxy.h"
#include <glm/vec3.hpp>

class UCesiumGltfPointsComponent;

/**
 * Used to pass tile data and Cesium3DTileset settings to a SceneProxy, usually
 * via render thread.
 */
struct FCesiumGltfPointsSceneProxyTilesetData {
  FCesiumPointCloudShading PointCloudShading;
  double MaximumScreenSpaceError;
  bool UsesAdditiveRefinement;
  float GeometricError;
  glm::vec3 Dimensions;

  FCesiumGltfPointsSceneProxyTilesetData();

  void UpdateFromComponent(UCesiumGltfPointsComponent* Component);
};

class FCesiumGltfPointsSceneProxy final : public FPrimitiveSceneProxy {
private:
  // The original render data of the static mesh.
  const FStaticMeshRenderData* RenderData;
  int32_t NumPoints;

public:
  SIZE_T GetTypeHash() const override;

  FCesiumGltfPointsSceneProxy(
      UCesiumGltfPointsComponent* InComponent,
      FSceneInterfaceWrapper InSceneInterfaceParams);

  virtual ~FCesiumGltfPointsSceneProxy();

protected:
  virtual void
  CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;

  virtual void DestroyRenderThreadResources() override;

  virtual void GetDynamicMeshElements(
      const TArray<const FSceneView*>& Views,
      const FSceneViewFamily& ViewFamily,
      uint32 VisibilityMap,
      FMeshElementCollector& Collector) const override;

  virtual FPrimitiveViewRelevance
  GetViewRelevance(const FSceneView* View) const override;

  virtual uint32 GetMemoryFootprint(void) const override;

  // Keeps the compute-raster registry entry's LocalToWorld current (origin
  // rebasing / georeference moves). Fires even while main-pass draw is gated off.
  virtual void OnTransformChanged(FRHICommandListBase& RHICmdList) override;

public:
  void UpdateTilesetData(
      const FCesiumGltfPointsSceneProxyTilesetData& InTilesetData);

private:
  // Whether or not the shader platform supports attenuation.
  bool bAttenuationSupported;

  // Data from the UCesiumGltfComponent that owns this scene proxy, as well as
  // its ACesium3DTileset.
  FCesiumGltfPointsSceneProxyTilesetData TilesetData;

  // The vertex factory and index buffer for point attenuation.
  FCesiumPointAttenuationVertexFactory AttenuationVertexFactory;
  FCesiumPointAttenuationIndexBuffer AttenuationIndexBuffer;

  // Slot in FCesiumPointProxyRegistry while the compute raster path is built,
  // or INDEX_NONE. Registered on render-thread resource creation.
  int32 ComputeRasterTileSlot = INDEX_NONE;

  // Cached from the component at construction; copied into the registry entry.
  bool bHasRealNormals = false;

  UMaterialInterface* Material;
  FMaterialRelevance MaterialRelevance;

  float GetGeometricError() const;

  void CreatePointAttenuationUserData(
      FMeshBatchElement& BatchElement,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;

  void CreateMeshWithAttenuation(
      FMeshBatch& Mesh,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;
  void CreateMesh(FMeshBatch& Mesh) const;
};
