#pragma once
namespace city
{

struct TileLoadTaskState
{
    cesium::TileRenderResources* tile_load_list;
    Bvh* bvh_result; // whole structure should be copied into task
    ArrayResourcePoolHandle bvh_handle;
};

struct TileLoadTaskStateNode
{
    TileLoadTaskStateNode* next;
    TileLoadTaskStateNode* prev;
    async::AsyncTaskStatus<TileLoadTaskState>* task_state;
};

g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status);

g_internal void
tile_load_async(ArrayResourcePoolHandle& bvh_handle);
g_internal void
tile_load_update();
g_internal void
tile_load_unused_tiles_free();
} // namespace city
