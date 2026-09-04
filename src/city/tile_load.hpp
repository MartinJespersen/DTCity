#pragma once
namespace city
{

struct TileLoadTaskState
{
    cesium::TileRenderResources* tile;
    Bvh* bvh;
    ArrayResourcePoolHandle bvh_handle;
    U64 mesh_processor_generation;
    B32 gpu_upload_submitted;
};

struct TileLoadTaskStateNode
{
    TileLoadTaskStateNode* next;
    TileLoadTaskStateNode* prev;
    async::AsyncTaskStatus<TileLoadTaskState>* task_state;
};

g_internal void
tile_load_update(City* city, cesium::TilesetRenderer* tileset, ui::Camera* camera, Vec2U32 framebuffer_dim, RoadOverlayOption road_overlay_option);
g_internal void
tile_load_pending_work_update();

g_internal void
_tile_load_stale_meshes_schedule(cesium::TilesetRenderer* tileset, ArrayResourcePoolHandle tileset_handle, ArrayResourcePoolHandle& bvh_handle, U64 mesh_processor_generation);
g_internal B32
_tile_load_mesh_reprocess_task_start(cesium::TileRenderResources* tile, ArrayResourcePoolHandle tileset_handle, Bvh* bvh, ArrayResourcePoolHandle bvh_handle, U64 mesh_processor_generation);
g_internal void
_tile_load_task_completions_update();
g_internal B32
_tile_load_road_mesh_process(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, void* user_data, render::TileMesh* out_mesh);
g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load_mesh_reprocess_task(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status);
g_internal void
_tile_load_gpu_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx);
g_internal void
_tile_load_deallocated_tiles_release();
} // namespace city
