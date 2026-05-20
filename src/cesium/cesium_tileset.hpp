#pragma once

namespace cesium
{

struct RasterTileInfo
{
    const CesiumGltf::ImageAsset& image;
    const std::any& renderer_options;

    RasterTileInfo(const CesiumGltf::ImageAsset& image, const std::any& renderer_options) : image(image), renderer_options(renderer_options)
    {
    }
};

struct TileRenderData
{
    TileRenderData* next;
    TileRenderData* render_next;

    render::Model3DPipelineData render_data;

    bool compute_scheduled;
};

struct TileRasterOverlayAttachment
{
    TileRasterOverlayAttachment* next;

    const CesiumRasterOverlays::RasterOverlayTile* raster_tile;
    void* raster_renderer_resources;

    render::Handle texture_handle;

    S32 overlay_texture_coordinate_id;
    glm::dvec2 translation;
    glm::dvec2 scale;
};

struct TileRenderDataList
{
    TileRenderDataList* next;
    Arena* arena;
    bool tile_is_loaded;

    TileRenderData* first;
    TileRenderData* last;

    TileRasterOverlayAttachment* raster_overlay_first;
};

struct TilesetRenderer
{
    Cesium3DTilesSelection::Tileset* tileset[2];
    CesiumAsync::ITaskProcessor* task_processor;
    CesiumUtility::CreditSystem* credit_system;
    CesiumAsync::AsyncSystem async_system;

    glm::dmat4 ecef_to_local;
    glm::dmat4 local_to_ecef;

    // tiles to render list
    OS_Handle tiles_to_free_mutex;
    TileRenderDataList* tiles_to_free_stack;
    U32 tiles_to_free_stack_count;
    TileRenderDataList tile_to_show;
    U32 tiles_to_show_count;
};

struct TilesetRendererCreateContext
{
    TilesetRenderer* renderer;
    Cesium3DTilesSelection::TilesetExternals externals;
    Cesium3DTilesSelection::TilesetOptions options;
};

// Lifecycle
g_internal void
tileset_renderer_create(Arena* arena, TilesetRenderer* on_out_cesium, async::ThreadPool* threads, String8 url, F64 origin_longitude, F64 origin_latitude, F64 origin_height);
g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer);

// Update and rendering
g_internal void
tileset_update_view(TilesetRenderer* renderer, ui::Camera* camera, Vec2U32 viewport_size, F64 delta_time);

// Helper to convert cesium glTF to render data
g_internal TileRenderDataList*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local, const glm::dmat4& tile_transform, CesiumGeometry::Axis gltf_up_axis,
                           render::ThreadWorkerCmdCtx* thread_input);

g_internal render::BBoxDraw*
render_raster_tile_record(render::ThreadWorkerCmdCtx* thread_input, RasterTileInfo* tile_info);

g_internal B32
root_tileset_url_is_readable(String8 url);
} // namespace cesium
