#pragma once

namespace cesium
{

struct TileInfo
{
    const CesiumGltf::Model& model;
    const glm::dmat4 ecef_to_local;
    const glm::dmat4 tile_transform;

    TileInfo(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local,
             const glm::dmat4& transform)
        : model(model), ecef_to_local(ecef_to_local), tile_transform(transform)
    {
    }
};

struct TileRenderData
{
    TileRenderData* next;
    String8 tile_id;
    render::Model3DPipelineData render_data;
};

struct TileRenderDataList
{
    Arena* arena;
    bool tile_is_loaded;
    TileRenderData* first;
    TileRenderData* last;
};

struct TilesetRenderer
{
    std::shared_ptr<Cesium3DTilesSelection::Tileset> tileset;
    std::shared_ptr<CesiumAsync::ITaskProcessor> task_processor;
    CesiumAsync::AsyncSystem async_system;

    // Coordinate system for local rendering
    CesiumGeospatial::LocalHorizontalCoordinateSystem* local_coord_system;

    // Render resources
    TileRenderDataList loaded_tiles;
};

// Lifecycle
g_internal TilesetRenderer*
tileset_renderer_create(Arena* arena, async::Threads* threads, const char* tileset_url,
                        F64 origin_longitude, F64 origin_latitude, F64 origin_height);
g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer);

// Update and rendering
g_internal void
tileset_update_view(Arena* arena, TilesetRenderer* renderer, ui::Camera* camera,
                    Vec2U32 viewport_size, F64 delta_time);
g_internal void
tileset_render(TilesetRenderer* renderer, render::Handle road_segment_handle,
               render::Handle road_segment_buffer_handle);

// Helper to convert cesium glTF to render data
g_internal TileRenderDataList*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local,
                           const glm::dmat4& tile_transform, render::ThreadInput* thread_input);

g_internal void*
render_list_record(render::ThreadInput* thread_input, render::FuncData user_data);
} // namespace cesium
