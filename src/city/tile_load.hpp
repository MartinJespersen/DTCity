#pragma once

#include "async/async_task.hpp"
#include "render/render.hpp"
#include "base/base_container.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "base/base_strings.hpp"
#include "base/container.hpp"
#include "city/tile_bvh.hpp"

namespace cesium
{
struct TileRenderResources;
struct TilesetRenderer;
} // namespace cesium
namespace async
{
struct ThreadPool;
struct ThreadInfo;
template <typename T>
struct AsyncTaskStatus;
template <typename T>
struct AsyncTaskContinuation;
} // namespace async
namespace city
{
struct RoadBvh;
enum RoadOverlayOption : U32;
} // namespace city
namespace city
{

struct TileLoadState;

struct TileLoadTaskState
{
    TileLoadState* state;
    cesium::TileRenderResources* tile;
    RoadBvh* bvh;
    PoolHandle bvh_handle;
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

    ResourcePool<cesium::TilesetRenderer>* tileset_pool;
    ResourcePool<RoadBvh>* polygon_bvh_pool;

    cesium::TileRenderResources* tile_first;
    cesium::TileRenderResources* tile_last;
    U32 tile_count;

    cesium::TileRenderResources* gpu_work_stack;

    TileLoadTaskStateNode* task_first;
    TileLoadTaskStateNode* task_last;
    TileLoadTaskStateNode* task_free_list;
};

TileLoadState*
tile_load_create(async::ThreadPool* thread_pool, U32 tileset_capacity);
void
tile_load_destroy(TileLoadState* state);

// Records vertex, reordered index, and triangle-BVH uploads in the worker's
// existing command buffer. The entry becomes ready through its completion callback.
TileMeshBvhEntry
tile_load_mesh_upload(Arena* arena, render::ThreadWorkerCmdCtx* thread_ctx,
                      Buffer<const render::TileVertex> vertices, Buffer<const U32> indices, U64 mesh_revision);
g_internal PoolHandle
tile_load_streaming_begin(TileLoadState* state, String8 tileset_url, Rng2F64 bounds, S64 tileset_ion_asset_id,
                          U64 cache_byte_size);
g_internal B32
tile_load_streaming_end(TileLoadState* state, PoolHandle tileset_handle);
g_internal void
tile_load_update(TileLoadState* state, PoolHandle tileset_handle, PoolHandle bvh_handle,
                 PoolHandle camera_handle, B32 road_building_done, B32 road_overlay_changed,
                 RoadOverlayOption road_overlay_option, F64 delta_time);
void
tile_load_debug_ui_draw(TileLoadState* state, PoolHandle tileset_handle);

g_internal void
_tile_load_stale_meshes_schedule(TileLoadState* state, cesium::TilesetRenderer* tileset,
                                 PoolHandle tileset_handle, PoolHandle bvh_handle,
                                 U64 mesh_processor_generation);
g_internal B32
_tile_load_mesh_reprocess_task_start(TileLoadState* state, cesium::TileRenderResources* tile,
                                     PoolHandle tileset_handle, RoadBvh* bvh,
                                     PoolHandle bvh_handle, U64 mesh_processor_generation);
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
g_internal void
_tile_load_bvh_selection_update(TileLoadState* state, cesium::TilesetRenderer* tileset,
                                PoolHandle tileset_handle);
} // namespace city
