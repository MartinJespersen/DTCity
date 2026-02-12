// This file includes cesium-native headers early, before any conflicting definitions
// It must be included BEFORE container.hpp to avoid Buffer name collision
#pragma once
// Disable fmt Unicode check - the /utf-8 flag doesn't apply to external includes
#ifndef FMT_UNICODE
#define FMT_UNICODE 0
#endif

#include <memory>
#include <functional>
#include <vector>
#include <any>

#include "base/base_diagnostics.hpp"
#include <Cesium3DTilesSelection/Tileset.h>
#include <Cesium3DTilesSelection/Tile.h>
#include <Cesium3DTilesSelection/TileContent.h>
#include <Cesium3DTilesSelection/ViewState.h>
#include <Cesium3DTilesSelection/ViewUpdateResult.h>
#include <Cesium3DTilesSelection/TilesetExternals.h>
#include <Cesium3DTilesSelection/TilesetOptions.h>
#include <Cesium3DTilesSelection/IPrepareRendererResources.h>
#include <Cesium3DTilesContent/registerAllTileContentTypes.h>
#include <CesiumRasterOverlays/RasterOverlayTile.h>
#include <CesiumGltfReader/GltfReader.h>
#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltf/Accessor.h>
#include <CesiumGltf/Buffer.h>
#include <CesiumGltf/BufferView.h>
#include <CesiumGltf/ImageAsset.h>
#include <CesiumAsync/AsyncSystem.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/ITaskProcessor.h>
#include <CesiumUtility/IntrusivePointer.h>
#include <CesiumGeospatial/Ellipsoid.h>
#include <CesiumGeospatial/Cartographic.h>
#include <CesiumGeospatial/LocalHorizontalCoordinateSystem.h>
#include <CesiumCurl/CurlAssetAccessor.h>
