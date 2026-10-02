#pragma once
namespace city
{

struct TileLoadState;

struct TileLoadTaskState
{
    TileLoadState* state;
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

struct TileLoadGpuWork
{
    TileLoadState* state;
    cesium::TileRenderResources* tile;
};

struct TileLoadState
{
    Arena* arena;
    async::ThreadPool* thread_pool;

    ArrayResourcePool<cesium::TilesetRenderer>* tileset_pool;
    ArrayResourcePool<Bvh>* polygon_bvh_pool;

    cesium::TileRenderResources* tile_first;
    cesium::TileRenderResources* tile_last;
    U32 tile_count;

    cesium::TileRenderResources* gpu_work_stack;

    TileLoadTaskStateNode* task_first;
    TileLoadTaskStateNode* task_last;
    TileLoadTaskStateNode* task_free_list;
};

g_internal TileLoadState*
tile_load_create(async::ThreadPool* thread_pool, U32 tileset_capacity);
g_internal void
tile_load_destroy(TileLoadState* state);
g_internal ArrayResourcePoolHandle
tile_load_streaming_begin(TileLoadState* state, String8 tileset_url, Rng2F64 bounds, S64 tileset_ion_asset_id,
                          U64 cache_byte_size);
g_internal B32
tile_load_streaming_end(TileLoadState* state, ArrayResourcePoolHandle tileset_handle);
g_internal void
tile_load_update(TileLoadState* state, ArrayResourcePoolHandle tileset_handle, ArrayResourcePoolHandle bvh_handle,
                 ArrayResourcePoolHandle camera_handle, B32 road_building_done, B32 road_overlay_changed,
                 RoadOverlayOption road_overlay_option, F64 delta_time);
g_internal void
tile_load_debug_ui_draw(TileLoadState* state, ArrayResourcePoolHandle tileset_handle);

g_internal void
_tile_load_stale_meshes_schedule(TileLoadState* state, cesium::TilesetRenderer* tileset,
                                 ArrayResourcePoolHandle tileset_handle, ArrayResourcePoolHandle bvh_handle,
                                 U64 mesh_processor_generation);
g_internal B32
_tile_load_mesh_reprocess_task_start(TileLoadState* state, cesium::TileRenderResources* tile,
                                     ArrayResourcePoolHandle tileset_handle, Bvh* bvh,
                                     ArrayResourcePoolHandle bvh_handle, U64 mesh_processor_generation);
g_internal void
_tile_load_task_completions_update(TileLoadState* state);
g_internal B32
_tile_load_road_mesh_process(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, void* user_data,
                             render::TileMesh* out_mesh);
g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load_mesh_reprocess_task(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status);
g_internal void
_tile_load_gpu_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx);
g_internal void
_tile_load_deallocated_tiles_release(TileLoadState* state);
} // namespace city
