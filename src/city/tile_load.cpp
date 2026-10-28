#include "core_inc.hpp"
#include "tile_load.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "base/base_strings.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/cache.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "utility/utility_inc.hpp"
#include "async/async_inc.hpp"
#include "async/async_task.hpp"
#include "async/async_task_templates.hpp"
#include "render/render_inc.hpp"
#include "render/render.hpp"
#include "draw/draw.hpp"
#include "misc/misc_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "gltfw/gltfw.hpp"
#include "osm/osm.hpp"
#include "cesium/cesium_tileset.hpp"
#include "city/city_inc.hpp"
#include "city/tile_bvh.hpp"
#include "city/city.hpp"
#include "city/tessellation.hpp"
#include "entrypoint.hpp"
#include "debug_log.hpp"
#include <atomic>
#include <imgui.h>

namespace city
{

TileLoadState*
tile_load_create(async::ThreadPool* thread_pool, U32 tileset_capacity)
{
    Assert(thread_pool);

    Arena* arena = arena_alloc();
    Debug_SetName(arena, "Tile Load State arena");
    TileLoadState* state = PushStruct(arena, TileLoadState);
    state->arena = arena;
    state->thread_pool = thread_pool;
    state->tileset_pool = ResourcePool<cesium::TilesetRenderer>::create(arena, tileset_capacity);
    state->polygon_bvh_pool = ResourcePool<RoadBvh>::create(arena, tileset_capacity);
    return state;
}

void
tile_load_destroy(TileLoadState* state)
{
    Assert(state);

    while (state->task_first || state->tile_count > 0)
    {
        render::gpu_work_update();
        _tile_load_task_completions_update(state);
    }

    render::gpu_work_done_wait();
    render::gpu_work_update();
    arena_release(state->arena);
}

TileMeshBvhEntry
tile_load_mesh_upload(Arena* arena, render::ThreadWorkerCmdCtx* thread_ctx,
                      Buffer<const render::TileVertex> vertices, Buffer<const U32> indices, U64 mesh_revision)
{
    // Staging copies are recorded before this scope ends. Reuse the worker's
    // existing arena lifetime rather than retaining CPU trees for every tile.
    Temp temp = temp_begin(arena);
    defer(temp_end(temp));
    TileMeshBvhBuild build = tile_mesh_bvh_build(arena, vertices, indices);
    render::MeshHandle mesh = {};
    render::Handle triangle_bvh_buffer = {};
    if (build.nodes.size > 0)
    {
        render::BufferInfo vertex_info(vertices, render::BufferType_Vertex | render::BufferType_StorageBuffer);
        render::BufferInfo index_info(build.indices, render::BufferType_Index | render::BufferType_StorageBuffer);
        render::BufferInfo node_info(build.nodes, render::BufferType_StorageBuffer);
        mesh.vertex_buffer_handle = render::buffer_load_sync(thread_ctx, &vertex_info, S("Tile Mesh Vertices"));
        mesh.index_buffer_handle = render::buffer_load_sync(thread_ctx, &index_info, S("Tile Mesh Indices"));
        triangle_bvh_buffer = render::buffer_load_sync(thread_ctx, &node_info, S("Tile Triangle BVH"));
    }
    TileMeshBvhEntry entry = tile_mesh_bvh_entry_create(vertices, indices, mesh, mesh_revision);
    entry.triangle_bvh_buffer = triangle_bvh_buffer;
    return entry;
}

g_internal PoolHandle
tile_load_streaming_begin(TileLoadState* state, String8 tileset_url, Rng2F64 bounds, S64 tileset_ion_asset_id,
                          U64 cache_byte_size)
{
    Assert(state);

    PoolHandle tileset_handle = state->tileset_pool->handle_get();
    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = state->tileset_pool->item_from_handle(tileset_handle, &tileset);
    Assert(tileset_exists);

    Vec2F64 bounds_center = {.x = (bounds.min.x + bounds.max.x) * 0.5, .y = (bounds.min.y + bounds.max.y) * 0.5};
    cesium::tileset_renderer_create(tileset, tileset_handle, state->thread_pool, tileset_url, bounds_center.x,
                                    bounds_center.y, 0.0, tileset_ion_asset_id, cache_byte_size);
    return tileset_handle;
}

g_internal B32
tile_load_streaming_end(TileLoadState* state, PoolHandle tileset_handle)
{
    Assert(state);

    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = state->tileset_pool->item_from_handle(tileset_handle, &tileset);
    if (tileset_exists)
    {
        B32 destroyed = cesium::tileset_renderer_destroy(tileset);
        if (!destroyed)
        {
            return false;
        }
        state->tileset_pool->item_free(tileset_handle);
    }
    return true;
}

// NOTE: To be called every frame on the render thread for an active city.
g_internal void
tile_load_update(TileLoadState* state, PoolHandle tileset_handle, PoolHandle bvh_handle,
                 PoolHandle camera_handle, B32 road_building_done, B32 road_overlay_changed,
                 RoadOverlayOption road_overlay_option, F64 delta_time)
{
    Assert(state);

    for (cesium::TilesetRenderer& registered_tileset : *state->tileset_pool)
    {
        cesium::tileset_pump_async(&registered_tileset);
    }

    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = state->tileset_pool->item_from_handle(tileset_handle, &tileset);
    tileset_exists = tileset_exists && !tileset->destruction_requested;
    if (tileset_exists)
    {
        cesium::tileset_update_view(tileset, camera_handle, delta_time);
    }

    _tile_load_task_completions_update(state);

    if (tileset_exists == false)
    {
        return;
    }

    _tile_load_bvh_selection_update(state, tileset, tileset_handle);

    if (road_building_done)
    {
        if (tileset->tile_mesh_processor.func == 0)
        {
            RoadBvh* bvh = {};
            B32 bvh_exists = state->polygon_bvh_pool->item_from_handle(bvh_handle, &bvh);
            Assert(bvh_exists);

            cesium::TileMeshProcessor mesh_processor = {_tile_load_road_mesh_process, bvh};
            cesium::tileset_tile_mesh_processor_set(tileset, mesh_processor);
            // Installing classification inputs invalidates previously loaded meshes.
            tileset->tile_mesh_processor_generation.fetch_add(1, std::memory_order_release);
        }

        B32 road_overlay_enabled = road_overlay_option != RoadOverlayOption_None;
        // Overlay selection changes rendering, not the classification inputs.
        (void)road_overlay_changed;
        cesium::tileset_tile_mesh_processor_enabled_set(tileset, road_overlay_enabled);
        if (road_overlay_enabled)
        {
            U64 mesh_processor_generation = tileset->tile_mesh_processor_generation.load(std::memory_order_acquire);
            _tile_load_stale_meshes_schedule(state, tileset, tileset_handle, bvh_handle, mesh_processor_generation);
        }
    }
}

void
tile_load_debug_ui_draw(TileLoadState* state, PoolHandle tileset_handle)
{
    Assert(state);

    ImGui::Text("Cesium Tiles Alive: List Count: %d", state->tile_count);

    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = state->tileset_pool->item_from_handle(tileset_handle, &tileset);
    if (tileset_exists)
    {
        ImGui::Text("Tileset Renderer Show: %d active", tileset->tiles_to_show_count);
    }
}

g_internal void
_tile_load_stale_meshes_schedule(TileLoadState* state, cesium::TilesetRenderer* tileset,
                                 PoolHandle tileset_handle, PoolHandle bvh_handle,
                                 U64 mesh_processor_generation)
{
    RoadBvh* bvh = 0;
    if (state->polygon_bvh_pool->item_from_handle(bvh_handle, &bvh) && bvh->deletion_requested == false)
    {
        U32 active_task_count = 0;
        // A replacement occupies its slot until its GPU upload is installed.
        for (cesium::TileRenderResources* tile = state->tile_first; tile; tile = tile->next)
        {
            if (tile->tile_mesh_processor_pending_generation != 0)
                active_task_count++;
        }
        for (TileLoadTaskStateNode* node = state->task_first; node; node = node->next)
        {
            // Conservatively count CPU work as well, including overlap with uploads.
            active_task_count++;
        }

        // Tile mesh processing shares this pool with Cesium loading. Reserve at
        // least half of the workers so a bulk overlay update cannot stall tile
        // downloads and content preparation.
        U32 maximum_task_count = state->thread_pool->thread_count / 2;
        if (maximum_task_count == 0)
        {
            maximum_task_count = 1;
        }
        if (active_task_count >= maximum_task_count)
        {
            return;
        }

        U32 available_task_count = maximum_task_count - active_task_count;
        U32 tasks_started_count = 0;

        // Schedule visible tiles before cached tiles so an overlay change is reflected on screen first.
        for (cesium::TileRenderResources* tile = tileset->tile_to_show_first;
             tile && tasks_started_count < available_task_count; tile = tile->render_next)
        {
            tasks_started_count += _tile_load_mesh_reprocess_task_start(state, tile, tileset_handle, bvh, bvh_handle,
                                                                        mesh_processor_generation);
        }

        for (cesium::TileRenderResources* tile = state->tile_first; tile && tasks_started_count < available_task_count;
             tile = tile->next)
        {
            tasks_started_count += _tile_load_mesh_reprocess_task_start(state, tile, tileset_handle, bvh, bvh_handle,
                                                                        mesh_processor_generation);
        }

        if (tasks_started_count > 0)
        {
            bvh->loads_in_flight += tasks_started_count;
        }
    }
}

g_internal B32
_tile_load_mesh_reprocess_task_start(TileLoadState* state, cesium::TileRenderResources* tile,
                                     PoolHandle tileset_handle, RoadBvh* bvh,
                                     PoolHandle bvh_handle, U64 mesh_processor_generation)
{
    B32 task_started = false;
    if (bvh->deletion_requested == false && tile->tileset_handle == tileset_handle &&
        tile->tile_mesh_processor_generation != mesh_processor_generation &&
        tile->tile_mesh_processor_pending_generation == 0 && tile->to_be_dealloced.load() == false)
    {
        tile->tile_mesh_processor_pending_generation = mesh_processor_generation;
        Allocator task_allocator = Allocator::create();
        Arena* task_arena = task_allocator.arena;
        Debug_SetName(task_arena, "Tile Load Task arena");
        TileLoadTaskState* tile_task_state = PushStruct(task_arena, TileLoadTaskState);
        tile_task_state->state = state;
        tile_task_state->tile = tile;
        tile_task_state->bvh = bvh;
        tile_task_state->bvh_handle = bvh_handle;
        tile_task_state->mesh_processor_generation = mesh_processor_generation;

        async::AsyncTaskStatus<TileLoadTaskState>* tile_load_task =
            async::async_task_run(std::move(task_allocator), state->thread_pool, _tile_load_mesh_reprocess_task, tile_task_state,
                                  "Tile Mesh Reprocess Task");
        TileLoadTaskStateNode* node = state->task_free_list;
        if (node)
        {
            SLLStackPop(state->task_free_list);
            MemoryZeroStruct(node);
        }
        else
        {
            node = PushStruct(state->arena, TileLoadTaskStateNode);
        }
        node->task_state = tile_load_task;
        DLLPushBack(state->task_first, state->task_last, node);
        task_started = true;
    }
    return task_started;
}

g_internal void
_tile_load_task_completions_update(TileLoadState* state)
{
    // Tasks that are done should be further processed.
    TileLoadTaskStateNode* next_node;
    for (TileLoadTaskStateNode* node = state->task_first; node; node = next_node)
    {
        next_node = node->next;
        if (auto completed = async::async_task_take_completed(node->task_state))
        {
            Assert(!completed->error);
            TileLoadTaskState* task_state = completed->user_data;
            if (task_state->gpu_upload_submitted == false)
            {
                task_state->tile->tile_mesh_processor_generation = task_state->mesh_processor_generation;
                task_state->tile->tile_mesh_processor_pending_generation = 0;
            }

            RoadBvh* bvh = task_state->bvh;
            Assert(bvh->loads_in_flight > 0);
            bvh->loads_in_flight--;
            if (bvh->loads_in_flight == 0 && bvh->deletion_requested)
            {
                arena_release(bvh->arena);
                state->polygon_bvh_pool->item_free(task_state->bvh_handle);
            }

            DLLRemove(state->task_first, state->task_last, node);
            SLLStackPush(state->task_free_list, node);
        }
    }

    // Finish jobs whose commands are done executing on the GPU.
    for (cesium::TileRenderResources* tile = state->gpu_work_stack; tile; tile = tile->gpu_work_done_next)
    {
        tile->tile_mesh_processor_generation = tile->tile_mesh_processor_pending_generation;
        tile->tile_mesh_processor_pending_generation = 0;

        for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
        {
            if (batch->has_load_replacement.load())
            {
                render::handle_destroy_deferred(batch->render_data.vertex_buffer_render_handle);
                render::handle_destroy_deferred(batch->render_data.index_buffer_render_handle);
                render::handle_destroy_deferred(batch->mesh_bvh_entry.triangle_bvh_buffer);
                batch->render_data.vertex_buffer_render_handle = batch->mesh_bvh_entry_load_temp.mesh.vertex_buffer_handle;
                batch->render_data.index_buffer_render_handle = batch->mesh_bvh_entry_load_temp.mesh.index_buffer_handle;
                batch->render_data.index_count = batch->mesh_bvh_entry_load_temp.triangle_count * 3;
                // Publish metadata with the matching GPU mesh and preserve owner registration.
                batch->mesh_bvh_entry_load_temp.handle = batch->mesh_bvh_entry.handle;
                batch->mesh_bvh_entry_load_temp.query_enabled = batch->mesh_bvh_entry.query_enabled;
                batch->mesh_bvh_entry = batch->mesh_bvh_entry_load_temp;
                if (tile->tile_bvh)
                    tile->tile_bvh->replace(batch->mesh_bvh_entry);
                batch->mesh_bvh_entry_load_temp = {};
                batch->has_load_replacement.store(false);
            }
        }
    }
    state->gpu_work_stack = {};

    _tile_load_deallocated_tiles_release(state);
}

g_internal void
_tile_load_deallocated_tiles_release(TileLoadState* state)
{
    cesium::TileRenderResources* tile_next = {};
    for (cesium::TileRenderResources* tile = state->tile_first; tile; tile = tile_next)
    {
        tile_next = tile->next;
        if (tile->to_be_dealloced.load() && tile->tile_mesh_processor_pending_generation == 0)
        {
            DLLRemove(state->tile_first, state->tile_last, tile);
            state->tile_count--;
            cesium::tileset_render_resources_release(tile);
        }
    }
}

g_internal B32
_tile_load_road_mesh_process(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, void* user_data,
                             render::TileMesh* out_mesh)
{
    RoadBvh* bvh = (RoadBvh*)user_data;
    B32 result = city::tesselate_roads(arena, vertices, indices, *bvh, out_mesh);
    return result;
}

g_internal void
_tile_load_gpu_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx)
{
    TileLoadGpuWork* gpu_work = (TileLoadGpuWork*)thread_ctx->user_data;
    SLLStackPush_N(gpu_work->state->gpu_work_stack, gpu_work->tile, gpu_work_done_next);
}

g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load_mesh_reprocess_task(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status)
{
    (void)info;
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadWorkerCmdCtx* thread_ctx = nullptr;

    cesium::TileRenderResources* tile = status->user_data->tile;
    RoadBvh* bvh = status->user_data->bvh;
    for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
    {
        render::TileMesh mesh = {};
        B32 has_road_classification =
            city::tesselate_roads(scratch.arena, batch->vertex_buffer_orig, batch->index_buffer_orig, *bvh, &mesh);

        if (has_road_classification)
        {
            if (!thread_ctx)
            {
                thread_ctx = render::thread_ctx_create();
                render::thread_cmd_buffer_record(thread_ctx);
                TileLoadGpuWork* gpu_work = PushStruct(thread_ctx->arena, TileLoadGpuWork);
                gpu_work->state = status->user_data->state;
                gpu_work->tile = tile;
                thread_ctx->user_data = gpu_work;
                thread_ctx->gpu_work_done_func = _tile_load_gpu_upload_complete;
                status->user_data->gpu_upload_submitted = true;
            }

            batch->mesh_bvh_entry_load_temp = tile_load_mesh_upload(
                scratch.arena, thread_ctx,
                {mesh.vertices.data, mesh.vertices.size}, {mesh.indices.data, mesh.indices.size},
                status->user_data->mesh_processor_generation);
            batch->has_load_replacement.store(true);
        }
    }

    if (thread_ctx)
    {
        render::thread_cmd_buffer_end(thread_ctx);
    }

    return {};
}

g_internal void
_tile_load_bvh_selection_update(TileLoadState* state, cesium::TilesetRenderer* tileset,
                                PoolHandle tileset_handle)
{
    // Cache residency does not imply query membership. Mark the draw list once,
    // including fading tiles, then update only entries whose selection changed.
    U64 draw_frame = ++tileset->bvh_draw_frame;
    for (cesium::TileRenderResources* tile = tileset->tile_to_show_first; tile; tile = tile->render_next)
        tile->bvh_draw_frame = draw_frame;
    for (cesium::TileRenderResources* tile = state->tile_first; tile; tile = tile->next)
    {
        if (tile->tileset_handle.idx != tileset_handle.idx ||
            tile->tileset_handle.gen_id != tileset_handle.gen_id || !tile->tile_bvh)
            continue;
        B32 query_enabled = tile->bvh_draw_frame == draw_frame && !tile->to_be_dealloced.load();
        for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
        {
            TileMeshBvhEntry& entry = batch->mesh_bvh_entry;
            if (entry.query_enabled != query_enabled)
            {
                entry.query_enabled = query_enabled;
                tile->tile_bvh->replace(entry);
            }
        }
    }
}

} // namespace city
