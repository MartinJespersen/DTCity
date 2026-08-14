namespace city
{

g_internal async::AsyncTaskContinuation<TileLoadTaskState>
_tile_load(async::ThreadInfo info, async::AsyncTaskStatus<TileLoadTaskState>* status)
{
    (void)info;
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadWorkerCmdCtx* thread_ctx = nullptr;

    cesium::TileRenderResources* tile = status->user_data->tile_load_list;
    Bvh* bvh = status->user_data->bvh_result;
    for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
    {
        render::Mesh mesh = {};
        bool has_road_classification = city::tesselate_roads(scratch.arena, batch->vertex_buffer_orig, batch->index_buffer_orig, *bvh, &mesh);

        if (has_road_classification)
        {
            if (!thread_ctx)
            {
                thread_ctx = render::thread_ctx_create();
                render::thread_cmd_buffer_record(thread_ctx);
            }

            render::BufferInfo vertex_buffer_info = render::BufferInfo(mesh.vertices, render::BufferType_Vertex);
            render::BufferInfo index_buffer_info = render::BufferInfo(mesh.indices, render::BufferType_Index);
            render::Handle vertex_buffer_render_handle = render::buffer_load_sync(thread_ctx, &vertex_buffer_info, S("Vertex Tile Load"));
            render::Handle index_buffer_render_handle = render::buffer_load_sync(thread_ctx, &index_buffer_info, S("Index Tile Load"));

            batch->vertex_buffer_handle_load_temp = vertex_buffer_render_handle;
            batch->index_buffer_handle_load_temp = index_buffer_render_handle;
            batch->index_count_load_temp = (U32)mesh.indices.size;
            batch->has_load_replacement = true;
        }
    }

    if (thread_ctx)
    {
        render::thread_cmd_buffer_end(thread_ctx);
    }

    return {};
}

// NOTE: To be called for every road
g_internal void
tile_load_async(ArrayResourcePoolHandle& bvh_handle)
{
    Context* ctx = dt_ctx_get();

    // check whether any tile needs to be loaded again. If so, continue.
    bool has_new_tile_loads = false;
    for (cesium::TileRenderResources* tile = ctx->tile_first; tile; tile = tile->next)
    {
        if (tile->tile_has_loaded == false)
        {
            has_new_tile_loads = true;
            break;
        }
    }
    if (has_new_tile_loads == false)
    {
        return;
    }

    Bvh* bvh = 0;
    if (ctx->polygon_bvh_pool->item_from_handle(bvh_handle, &bvh))
    {
        // create task for tiles that needs to reload
        U32 tiles_to_load_count = 0;
        for (cesium::TileRenderResources* tile = ctx->tile_first; tile; tile = tile->next)
        {
            if (tile->tile_has_loaded == false)
            {
                tiles_to_load_count++;
                tile->tile_has_loaded = true;
                tile->is_tile_loading = true;
                Arena* task_arena = arena_alloc();
                Debug_SetName(task_arena, "Tile Load Task arena");
                TileLoadTaskState* tile_task_state = PushStruct(task_arena, TileLoadTaskState);
                tile_task_state->tile_load_list = tile;
                tile_task_state->bvh_result = bvh;
                tile_task_state->bvh_handle = bvh_handle;

                // add task to global list of loading tiles
                async::AsyncTaskStatus<TileLoadTaskState>* tile_load_task = async::async_task_run(task_arena, ctx->thread_pool, _tile_load, tile_task_state, "Tile Load Task");
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
            }
        }
        bvh->loads_in_flight += tiles_to_load_count;
        Assert(tiles_to_load_count > 0);
    }
}

// NOTE: To be called every frame on the render thread
g_internal void
tile_load_update()
{
    Context* ctx = dt_ctx_get();

    // tasks that are done should be further processed
    TileLoadTaskStateNode* next_node;
    for (TileLoadTaskStateNode* node = ctx->tile_load_task_first; node; node = next_node)
    {
        next_node = node->next;
        async::AsyncTaskResult<TileLoadTaskState> result = async::async_task_is_done(node->task_state);
        if (result.done)
        {
            Assert(result.success);
            TileLoadTaskState* task_state = result.task->user_data;
            cesium::TileRenderResources* tile = task_state->tile_load_list;
            tile->is_tile_loading = false;
            Assert(task_state->bvh_result->loads_in_flight > 0);
            task_state->bvh_result->loads_in_flight--;

            for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
            {
                if (batch->has_load_replacement)
                {
                    render::handle_destroy_deferred(batch->render_data.vertex_buffer_render_handle);
                    render::handle_destroy_deferred(batch->render_data.index_buffer_render_handle);
                    batch->render_data.vertex_buffer_render_handle = batch->vertex_buffer_handle_load_temp;
                    batch->render_data.index_buffer_render_handle = batch->index_buffer_handle_load_temp;
                    batch->render_data.index_count = batch->index_count_load_temp;
                    batch->vertex_buffer_handle_load_temp = {};
                    batch->index_buffer_handle_load_temp = {};
                    batch->index_count_load_temp = 0;
                    batch->has_load_replacement = false;
                }
            }

            DLLRemove(ctx->tile_load_task_first, ctx->tile_load_task_last, node);
            SLLStackPush(ctx->tile_load_task_free_list, node);

#if (0)
            // if task bvh handle is not the current handle in use free if no loads are in flight
            if (MemoryMatchStruct(&cur_bvh_handle, &task_state->bvh_handle) == 0 && task_state->bvh_result->loads_in_flight == 0)
            {
                Bvh* bvh = 0;
                if (ctx->polygon_bvh_pool->item_from_handle(task_state->bvh_handle, &bvh))
                {
                    arena_release(bvh->arena);
                    ctx->polygon_bvh_pool->item_free(task_state->bvh_handle);
                }
            }
#endif
        }
    }
    tile_load_unused_tiles_free();
}

g_internal void
tile_load_unused_tiles_free()
{
    Context* ctx = dt_ctx_get();

    cesium::TileRenderResources* tile_next = {};
    for (cesium::TileRenderResources* tile = ctx->tile_first; tile; tile = tile_next)
    {
        tile_next = tile->next;
        if (tile->to_be_dealloced.load() && (tile->is_tile_loading == false))
        {
            DLLRemove(ctx->tile_first, ctx->tile_last, tile);
            ctx->tile_count--;
            cesium::tileset_render_resources_release(tile);
        }
    }
}

} // namespace city
