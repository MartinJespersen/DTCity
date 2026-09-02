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

struct TileDrawBatch
{
    TileDrawBatch* next;

    Buffer<render::TileVertex> vertex_buffer_orig;
    Buffer<U32> index_buffer_orig;

    // Written to the load thread only
    render::Handle vertex_buffer_handle_load_temp;
    render::Handle index_buffer_handle_load_temp;
    U32 index_count_load_temp;
    B32 has_load_replacement;

    render::TilePipelineData render_data;
};

struct TileRenderResources
{
    TileRenderResources* next;
    TileRenderResources* prev;
    TileRenderResources* render_next;
    Arena* arena;

    // Cross thread communication
    bool tile_has_loaded;
    bool is_tile_loading;             // when 0 no loads are in flight and the object can be freed(pushed to free list)
    std::atomic<U32> to_be_dealloced; // this is set to true (not null) in cesium free

    bool tile_is_loaded;

    TileDrawBatch* batch_first;
    TileDrawBatch* batch_last;

    TileRasterOverlayAttachment* raster_overlay_first;
};

struct RasterRenderResource
{
    render::BBoxDraw draw;
    RasterRenderResource* active_next;
};

struct TilesetRenderer
{
    Allocator* allocator;

    Buffer<Cesium3DTilesSelection::Tileset*> tilesets;
    CesiumAsync::ITaskProcessor* task_processor;
    CesiumUtility::CreditSystem* credit_system;
    CesiumAsync::AsyncSystem async_system;

    glm::dmat4 ecef_to_local;
    glm::dmat4 local_to_ecef;

    // height delta at the bounding box center between the custom geometry tileset[0]
    // surface and the cesium ion terrain tileset[1] surface (tileset[0] height - terrain height)
    F64 height_offset;
    // set by tileset_renderer_destroy to end the recurring height_offset sampling loop
    B32 height_sample_stop;

    // tiles access from main thread
    TileRenderResources* tile_to_show_first;
    TileRenderResources* tile_to_show_last;
    U32 tiles_to_show_count;
    U64 tile_draw_batch_id_counter;

    RasterRenderResource* active_raster_resource_first;
};

// Lifecycle
g_internal void
tileset_renderer_create(TilesetRenderer* tileset, async::ThreadPool* threads, String8 url, F64 origin_longitude, F64 origin_latitude, F64 origin_height, bool custom_geometry_enabled,
                        U64 cache_byte_size);
g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer);

// Update and rendering
g_internal void
tileset_pump_async(TilesetRenderer* renderer);
g_internal void
tileset_update_view(TilesetRenderer* renderer, ui::Camera* camera, Vec2U32 viewport_size, F64 delta_time);

// Helper to convert cesium glTF to render data
g_internal TileRenderResources*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local, const glm::dmat4& tile_transform, CesiumGeometry::Axis gltf_up_axis,
                           render::ThreadWorkerCmdCtx* thread_input);

g_internal RasterRenderResource*
render_raster_tile_record(render::ThreadWorkerCmdCtx* thread_input, RasterTileInfo* tile_info);

g_internal F64
sample_height_from_result(const Cesium3DTilesSelection::SampleHeightResult& result, const char* label);

g_internal void
tileset_renderer_free_handles(TileRenderResources* list);

g_internal void
tileset_render_resources_release(TileRenderResources* list);

g_internal Rng3F32
_tile_local_bounds_get(const Cesium3DTilesSelection::Tile& tile, const CesiumGeospatial::Ellipsoid& ellipsoid, const glm::dmat4& ecef_to_local);

g_internal void
_tileset_renderer_initialize(TilesetRenderer* tileset, async::ThreadPool* threads, F64 origin_longitude, F64 origin_latitude, F64 origin_height);

g_internal Cesium3DTilesSelection::TilesetExternals
_tileset_externals_create(TilesetRenderer* tileset);

g_internal Cesium3DTilesSelection::TilesetOptions
_tileset_options_create(U64 cache_byte_size);

g_internal void
_tileset_renderer_tile_to_show_push(TilesetRenderer* renderer, const Cesium3DTilesSelection::Tile& tile, B32 is_fading_out);

g_internal void
_tileset_renderer_raster_resource_track(TilesetRenderer* renderer, RasterRenderResource* resource);

g_internal B32
_tileset_renderer_raster_resource_untrack(TilesetRenderer* renderer, RasterRenderResource* resource);
} // namespace cesium
