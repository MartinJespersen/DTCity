namespace city
{

// NOTE: To be called every frame on the render thread for an active city.
g_internal void
tile_load_update(City* city, cesium::TilesetRenderer* tileset, ui::Camera* camera, Vec2U32 framebuffer_dim, RoadOverlayOption road_overlay_option)
{
    Assert(city);
    Assert(tileset);
    Assert(camera);

    Context* ctx = dt_ctx_get();
    cesium::tileset_update_view(tileset, camera, framebuffer_dim, ctx->time->time_delta_constant_sec);
    _tile_load_task_completions_update();

    if (city->road_building_done)
    {
        if (tileset->tile_mesh_processor.func == 0)
        {
            Bvh* bvh = {};
            B32 bvh_exists = ctx->polygon_bvh_pool->item_from_handle(city->road.bvh_handle, &bvh);
            Assert(bvh_exists);

            cesium::TileMeshProcessor mesh_processor = {_tile_load_road_mesh_process, bvh};
            cesium::tileset_tile_mesh_processor_set(tileset, mesh_processor);
        }

        B32 road_overlay_enabled = road_overlay_option != RoadOverlayOption_None;
        B32 road_overlay_was_enabled = tileset->tile_mesh_processor_enabled.load(std::memory_order_acquire);
        B32 road_overlay_option_changed = city->road.overlay_option_cur != road_overlay_option;
        B32 mesh_processor_changed = road_overlay_was_enabled == false || road_overlay_option_changed;
        if (road_overlay_enabled && mesh_processor_changed)
        {
            tileset->tile_mesh_processor_generation.fetch_add(1, std::memory_order_release);
        }
        cesium::tileset_tile_mesh_processor_enabled_set(tileset, road_overlay_enabled);
        if (road_overlay_enabled)
        {
            U64 mesh_processor_generation = tileset->tile_mesh_processor_generation.load(std::memory_order_acquire);
            _tile_load_stale_meshes_schedule(tileset, city->tileset_handle, city->road.bvh_handle, mesh_processor_generation);
        }
        city->road.overlay_option_cur = road_overlay_option;
    }
}

g_internal void
tile_load_pending_work_update()
{
    _tile_load_task_completions_update();
}

g_internal void
_tile_load_stale_meshes_schedule(cesium::TilesetRenderer* tileset, ArrayResourcePoolHandle tileset_handle, ArrayResourcePoolHandle& bvh_handle, U64 mesh_processor_generation)
{
    Context* ctx = dt_ctx_get();

    Bvh* bvh = 0;
    if (ctx->polygon_bvh_pool->item_from_handle(bvh_handle, &bvh) && bvh->deletion_requested == false)
    {
        U32 active_task_count = 0;
        for (TileLoadTaskStateNode* node = ctx->tile_load_task_first; node; node = node->next)
        {
            active_task_count++;
        }

        // Tile mesh processing shares this pool with Cesium loading. Reserve at
        // least half of the workers so a bulk overlay update cannot stall tile
        // downloads and content preparation.
        U32 maximum_task_count = ctx->thread_pool->thread_count / 2;
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
        for (cesium::TileRenderResources* tile = tileset->tile_to_show_first; tile && tasks_started_count < available_task_count; tile = tile->render_next)
        {
            tasks_started_count += _tile_load_mesh_reprocess_task_start(tile, tileset_handle, bvh, bvh_handle, mesh_processor_generation);
        }

        for (cesium::TileRenderResources* tile = ctx->tile_first; tile && tasks_started_count < available_task_count; tile = tile->next)
        {
            tasks_started_count += _tile_load_mesh_reprocess_task_start(tile, tileset_handle, bvh, bvh_handle, mesh_processor_generation);
        }

        if (tasks_started_count > 0)
        {
            bvh->loads_in_flight += tasks_started_count;
        }
    }
}

g_internal B32
_tile_load_mesh_reprocess_task_start(cesium::TileRenderResources* tile, ArrayResourcePoolHandle tileset_handle, Bvh* bvh, ArrayResourcePoolHandle bvh_handle, U64 mesh_processor_generation)
{
    B32 task_started = false;
    if (bvh->deletion_requested == false && tile->tileset_handle == tileset_handle && tile->tile_mesh_processor_generation != mesh_processor_generation &&
        tile->tile_mesh_processor_pending_generation == 0 && tile->to_be_dealloced.load() == false)
    {
        Context* ctx = dt_ctx_get();
        tile->tile_mesh_processor_pending_generation = mesh_processor_generation;
        Arena* task_arena = arena_alloc();
        Debug_SetName(task_arena, "Tile Load Task arena");
        TileLoadTaskState* tile_task_state = PushStruct(task_arena, TileLoadTaskState);
        tile_task_state->tile = tile;
        tile_task_state->bvh = bvh;
        tile_task_state->bvh_handle = bvh_handle;
        tile_task_state->mesh_processor_generation = mesh_processor_generation;

        async::AsyncTaskStatus<TileLoadTaskState>* tile_load_task = async::async_task_run(task_arena, ctx->thread_pool, _tile_load_mesh_reprocess_task, tile_task_state, "Tile Mesh Reprocess Task");
        TileLoadTaskStateNode* node = ctx->tile_load_task_free_list;
        if (node)
        {
            SLLStackPop(ctx->tile_load_task_free_list);
            MemoryZeroStruct(node);
        }
        else
        {
            node = PushStruct(ctx->arena, TileLoadTaskStateNode);
        }
        node->task_state = tile_load_task;
        DLLPushBack(ctx->tile_load_task_first, ctx->tile_load_task_last, node);
        task_started = true;
    }
    return task_started;
}

g_internal void
_tile_load_task_completions_update()
{
    Context* ctx = dt_ctx_get();

    // Tasks that are done should be further processed.
    TileLoadTaskStateNode* next_node;
    for (TileLoadTaskStateNode* node = ctx->tile_load_task_first; node; node = next_node)
    {
        next_node = node->next;
        async::AsyncTaskResult<TileLoadTaskState> result = async::async_task_is_done(node->task_state);
        if (result.done)
        {
            Assert(result.success);
            TileLoadTaskState* task_state = result.task->user_data;
            if (task_state->gpu_upload_submitted == false)
            {
                task_state->tile->tile_mesh_processor_generation = task_state->mesh_processor_generation;
                task_state->tile->tile_mesh_processor_pending_generation = 0;
            }

            Bvh* bvh = task_state->bvh;
            Assert(bvh->loads_in_flight > 0);
            bvh->loads_in_flight--;
            if (bvh->loads_in_flight == 0 && bvh->deletion_requested)
            {
                arena_release(bvh->arena);
                ctx->polygon_bvh_pool->item_free(task_state->bvh_handle);
            }

            DLLRemove(ctx->tile_load_task_first, ctx->tile_load_task_last, node);
            SLLStackPush(ctx->tile_load_task_free_list, node);
        }
    }

    // Finish jobs whose commands are done executing on the GPU.
    for (cesium::TileRenderResources* tile = ctx->gpu_work_stack; tile; tile = tile->gpu_work_done_next)
    {
        tile->tile_mesh_processor_generation = tile->tile_mesh_processor_pending_generation;
        tile->tile_mesh_processor_pending_generation = 0;

        for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
        {
            if (batch->has_load_replacement.load())
            {
                render::handle_destroy_deferred(batch->render_data.vertex_buffer_render_handle);
                render::handle_destroy_deferred(batch->render_data.index_buffer_render_handle);
                batch->render_data.vertex_buffer_render_handle = batch->vertex_buffer_handle_load_temp;
                batch->render_data.index_buffer_render_handle = batch->index_buffer_handle_load_temp;
                batch->render_data.index_count = batch->index_count_load_temp;
                batch->vertex_buffer_handle_load_temp = {};
                batch->index_buffer_handle_load_temp = {};
                batch->index_count_load_temp = 0;
                batch->has_load_replacement.store(false);
            }
        }
    }
    ctx->gpu_work_stack = {};

    _tile_load_deallocated_tiles_release();
}

g_internal void
_tile_load_deallocated_tiles_release()
{
    Context* ctx = dt_ctx_get();

    cesium::TileRenderResources* tile_next = {};
    for (cesium::TileRenderResources* tile = ctx->tile_first; tile; tile = tile_next)
    {
        tile_next = tile->next;
        if (tile->to_be_dealloced.load() && tile->tile_mesh_processor_pending_generation == 0)
        {
            DLLRemove(ctx->tile_first, ctx->tile_last, tile);
            ctx->tile_count--;
            cesium::tileset_render_resources_release(tile);
        }
    }
}

g_internal B32
_tile_load_road_mesh_process(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, void* user_data, render::TileMesh* out_mesh)
{
    Bvh* bvh = (Bvh*)user_data;
    B32 result = city::tesselate_roads(arena, vertices, indices, *bvh, out_mesh);
    return result;
}

g_internal void
_tile_load_gpu_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx)
{
    Context* ctx = dt_ctx_get();
    cesium::TileRenderResources* tile = (cesium::TileRenderResources*)thread_ctx->user_data;
    SLLStackPush(ctx->gpu_work_stack, tile);
}

g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load_mesh_reprocess_task(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status)
{
    (void)info;
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadWorkerCmdCtx* thread_ctx = nullptr;

    cesium::TileRenderResources* tile = status->user_data->tile;
    Bvh* bvh = status->user_data->bvh;
    for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
    {
        render::TileMesh mesh = {};
        B32 has_road_classification = city::tesselate_roads(scratch.arena, batch->vertex_buffer_orig, batch->index_buffer_orig, *bvh, &mesh);

        if (has_road_classification)
        {
            if (!thread_ctx)
            {
                thread_ctx = render::thread_ctx_create();
                render::thread_cmd_buffer_record(thread_ctx);
                thread_ctx->user_data = tile;
                thread_ctx->gpu_work_done_func = _tile_load_gpu_upload_complete;
                status->user_data->gpu_upload_submitted = true;
            }

            render::BufferInfo vertex_buffer_info = render::BufferInfo(mesh.vertices, render::BufferType_Vertex);
            render::BufferInfo index_buffer_info = render::BufferInfo(mesh.indices, render::BufferType_Index);
            render::Handle vertex_buffer_render_handle = render::buffer_load_sync(thread_ctx, &vertex_buffer_info, S("Vertex Tile Load"));
            render::Handle index_buffer_render_handle = render::buffer_load_sync(thread_ctx, &index_buffer_info, S("Index Tile Load"));

            batch->vertex_buffer_handle_load_temp = vertex_buffer_render_handle;
            batch->index_buffer_handle_load_temp = index_buffer_render_handle;
            batch->index_count_load_temp = (U32)mesh.indices.size;
            batch->has_load_replacement.store(true);
        }
    }

    if (thread_ctx)
    {
        render::thread_cmd_buffer_end(thread_ctx);
    }

    return {};
}

} // namespace city
