#pragma once

namespace cesium
{

struct TileInfo
{
    const CesiumGltf::Model& model;
    const glm::dmat4 ecef_to_local;
    const glm::dmat4 tile_transform;

    TileInfo(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local, const glm::dmat4& transform) : model(model), ecef_to_local(ecef_to_local), tile_transform(transform)
    {
    }
};

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
    Cesium3DTilesSelection::Tileset* tileset;
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

// Lifecycle
g_internal TilesetRenderer*
tileset_renderer_create(Arena* arena, async::ThreadPool* threads, const char* tileset_url, F64 origin_longitude, F64 origin_latitude, F64 origin_height);
g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer);

// Update and rendering
g_internal void
tileset_update_view(Arena* arena, TilesetRenderer* renderer, ui::Camera* camera, Vec2U32 viewport_size, F64 delta_time);

// Helper to convert cesium glTF to render data
g_internal TileRenderDataList*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local, const glm::dmat4& tile_transform, render::ThreadInput* thread_input);

g_internal void*
render_raster_tile_record(render::ThreadInput* thread_input, render::FuncData user_data);
g_internal void*
render_list_record(render::ThreadInput* thread_input, render::FuncData user_data);
} // namespace cesium
