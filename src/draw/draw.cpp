namespace draw
{

g_internal Draw g_draw_ctx = {};

g_internal void
draw_init()
{
    if (!g_draw_ctx.frame_arena)
    {
        g_draw_ctx.frame_arena = arena_alloc();
    }
}

g_internal void
draw_release()
{
    if (g_draw_ctx.frame_arena)
    {
        arena_release(g_draw_ctx.frame_arena);
    }
    g_draw_ctx = {};
}

g_internal void
draw_new_frame()
{
    arena_clear(g_draw_ctx.frame_arena);
    g_draw_ctx.frame = PushStruct(g_draw_ctx.frame_arena, DrawFrame);
}

g_internal Arena*
draw_frame_arena_get()
{
    return g_draw_ctx.frame_arena;
}

g_internal DrawFrame*
draw_frame_get()
{
    if (!g_draw_ctx.frame)
    {
        draw_new_frame();
    }
    return g_draw_ctx.frame;
}

g_internal void
draw_flush()
{
    DrawFrame* frame = draw_frame_get();
    for (RoadIntersectionNode* node = frame->road_intersection_list.first; node; node = node->next)
    {
        render::road_intersection_compute_add(node->storage_buffer_handle, node->index_buffer_handle, node->road_segment_buffer_handle, node->road_segment_node_buffer_handle,
                                              node->road_segment_handle, node->overlay_option);
    }

    for (Model3DNode* node = frame->model_3D_list.first; node; node = node->next)
    {
        render::model_3d_draw(node->pipeline_input, node->colormap_handle);
    }

    for (Blend3DNode* node = frame->blend_3d_list.first; node; node = node->next)
    {
        render::blend_3d_draw(node->pipeline_input);
    }

    B32 car_instance_render_queued = false;
    for (CarInstanceRenderNode* node = frame->car_instance_render_list.list.first; node; node = node->next)
    {
        if (render::car_instance_render_bucket_add(node->vertex_buffer_handle, node->index_buffer_handle, node->texture_handle, &node->instance_buffer_info, node->instance_buffer_offset))
        {
            car_instance_render_queued = true;
        }
    }

    if (car_instance_render_queued)
    {
        for (CarInstanceComputeNode* node = frame->car_instance_compute_list.list.first; node; node = node->next)
        {
            render::car_instance_compute_bucket_add(&node->instance_buffer_info, node->tile_vertex_buffer_handle, node->tile_index_buffer_handle, node->car_center_to_road_offset,
                                                    node->instance_buffer_offset);
        }
    }

    for (LinkedListNode<render::BBoxDraw>* node = frame->bbox_list.first; node; node = node->next)
    {
        render::render_bbox_3d(&node->v);
    }
}

g_internal void
draw_model_3d(render::Model3DPipelineData pipeline_input, render::Handle colormap_handle)
{
    DrawFrame* frame = draw_frame_get();
    Model3DNode* node = PushStruct(g_draw_ctx.frame_arena, Model3DNode);
    node->pipeline_input = pipeline_input;
    node->colormap_handle = colormap_handle;
    SLLQueuePush(frame->model_3D_list.first, frame->model_3D_list.last, node);
}

g_internal void
draw_blend_3d(render::Blend3DPipelineData pipeline_input)
{
    DrawFrame* frame = draw_frame_get();
    Blend3DNode* node = PushStruct(g_draw_ctx.frame_arena, Blend3DNode);
    node->pipeline_input = pipeline_input;
    SLLQueuePush(frame->blend_3d_list.first, frame->blend_3d_list.last, node);
}

g_internal bool
draw_road_intersection_compute(render::Handle storage_buffer_handle, render::Handle index_buffer_handle, render::Handle road_segment_buffer_handle, render::Handle road_segment_node_buffer_handle,
                               render::Handle road_segment_handle, U32 overlay_option)
{
    if (render::is_handle_zero(storage_buffer_handle) || render::is_handle_zero(index_buffer_handle) || render::is_handle_zero(road_segment_handle) ||
        render::is_handle_zero(road_segment_buffer_handle) || render::is_handle_zero(road_segment_node_buffer_handle))
    {
        return false;
    }

    if (!render::is_resource_loaded(storage_buffer_handle) || !render::is_resource_loaded(index_buffer_handle) || !render::is_resource_loaded(road_segment_handle) ||
        !render::is_resource_loaded(road_segment_buffer_handle) || !render::is_resource_loaded(road_segment_node_buffer_handle))
    {
        return false;
    }

    DrawFrame* frame = draw_frame_get();
    RoadIntersectionNode* node = PushStruct(g_draw_ctx.frame_arena, RoadIntersectionNode);
    node->storage_buffer_handle = storage_buffer_handle;
    node->index_buffer_handle = index_buffer_handle;
    node->road_segment_buffer_handle = road_segment_buffer_handle;
    node->road_segment_node_buffer_handle = road_segment_node_buffer_handle;
    node->road_segment_handle = road_segment_handle;
    node->overlay_option = overlay_option;
    SLLQueuePush(frame->road_intersection_list.first, frame->road_intersection_list.last, node);

    return true;
}

g_internal CarInstanceRenderAddResult
draw_car_instance_render(render::Handle vertex_buffer_handle, render::Handle index_buffer_handle, render::Handle tex_handle, render::BufferInfo* instance_buffer_info)
{
    CarInstanceRenderAddResult result = {};
    if (instance_buffer_info->buffer.size == 0 || instance_buffer_info->elem_count == 0)
    {
        return result;
    }

    if (!render::is_resource_loaded(vertex_buffer_handle) || !render::is_resource_loaded(index_buffer_handle) || !render::is_resource_loaded(tex_handle))
    {
        return result;
    }

    DrawFrame* frame = draw_frame_get();
    CarInstanceRender* instance_draw = &frame->car_instance_render_list;
    U32 align = 16;
    U32 instance_buffer_offset = instance_draw->total_instance_buffer_byte_count + (align - 1);
    instance_buffer_offset -= instance_buffer_offset % align;

    CarInstanceRenderNode* node = PushStruct(g_draw_ctx.frame_arena, CarInstanceRenderNode);
    node->vertex_buffer_handle = vertex_buffer_handle;
    node->index_buffer_handle = index_buffer_handle;
    node->texture_handle = tex_handle;
    node->instance_buffer_info = *instance_buffer_info;
    node->instance_buffer_offset = instance_buffer_offset;
    instance_draw->total_instance_buffer_byte_count = Max(instance_draw->total_instance_buffer_byte_count, instance_buffer_offset + instance_buffer_info->buffer.size);
    SLLQueuePush(instance_draw->list.first, instance_draw->list.last, node);

    result.queued = true;
    result.instance_buffer_offset = instance_buffer_offset;
    return result;
}

g_internal void
draw_car_instance_compute(render::BufferInfo* instance_buffer_info, render::Handle tile_vertex_buffer_handle, render::Handle tile_index_buffer_handle, F32 car_center_to_road_offset,
                          U32 instance_buffer_offset)
{
    if (instance_buffer_info->buffer.size == 0 || instance_buffer_info->elem_count == 0)
    {
        return;
    }

    if (!render::is_resource_loaded(tile_vertex_buffer_handle) || !render::is_resource_loaded(tile_index_buffer_handle))
    {
        return;
    }

    DrawFrame* frame = draw_frame_get();
    CarInstanceComputeNode* node = PushStruct(g_draw_ctx.frame_arena, CarInstanceComputeNode);
    node->instance_buffer_info = *instance_buffer_info;
    node->tile_vertex_buffer_handle = tile_vertex_buffer_handle;
    node->tile_index_buffer_handle = tile_index_buffer_handle;
    node->car_center_to_road_offset = car_center_to_road_offset;
    node->instance_buffer_offset = instance_buffer_offset;
    SLLQueuePush(frame->car_instance_compute_list.list.first, frame->car_instance_compute_list.list.last, node);
}

g_internal void
_draw_bbox_3d_prepare(async::ThreadInfo thread_info, async::WorkerData* data)
{
    (void)thread_info;
    render::BBoxDraw* bbox_draw = (render::BBoxDraw*)data;
    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_input_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer({ render::thread_cmd_buffer_end(thread_ctx); });
    constexpr U32 meters_per_patch_side = 64;
    constexpr U32 patch_coords_num = 4;

    glm::uvec2 patch_dim = glm::uvec2(glm::ceil(bbox_draw->bbox.size * glm::vec2(meters_per_patch_side)));

    // divided into sub primitives
    U64 patch_num = (U64)patch_dim.x * (U64)patch_dim.y;

    // Create smaller quads
    Buffer<glm::vec2> patch_buffer = buffer_alloc<glm::vec2>(bbox_draw->arena, patch_num * patch_coords_num);
    for (U32 i = 0; i < patch_dim.x; ++i)
    {
        U32 width_offset = i * meters_per_patch_side;
        for (U32 j = 0; j < patch_dim.y; ++i)
        {
            U32 height_offset = j * meters_per_patch_side;

            glm::vec2* btm_lt = patch_buffer[i * patch_dim.x + j * patch_coords_num];
            glm::vec2* btm_rt = btm_lt + 1;
            glm::vec2* top_lt = btm_lt + 2;
            glm::vec2* top_rt = btm_lt + 3;

            F32 width = Min(width_offset + meters_per_patch_side, bbox_draw->bbox.size.x);
            F32 height = Min(height_offset + meters_per_patch_side, bbox_draw->bbox.size.y);
            *btm_lt = glm::vec2(bbox_draw->bbox.btm_lt_pos) * glm::vec2(width_offset, height_offset);
            *btm_rt = glm::vec2(bbox_draw->bbox.btm_lt_pos) * glm::vec2(width, height_offset);
            *top_lt = glm::vec2(bbox_draw->bbox.btm_lt_pos) * glm::vec2(width_offset, height);
            *top_rt = glm::vec2(bbox_draw->bbox.btm_lt_pos) * glm::vec2(width, height);
        }
    }
    // Buffer render::buffer_load_sync(thread_ctx->cmd_buffer, );
    // make buffer ref the async draw queue
    // schedule task to draw queue
    //
}

g_internal void
draw_bbox_3d_schedule(Arena* arena, render::Quad2F64* bbox)
{
    // Draw* draw_ctx = &g_draw_ctx;
    // LinkedList<render::BBoxDraw>* bbox_draw_list = &draw_ctx->frame->bbox_list;
    // if (render::is_resource_loaded(tex))
    // {
    //     LinkedListNode<render::BBoxDraw>* node = PushStruct(draw_ctx->frame_arena, LinkedListNode<render::BBoxDraw>);
    //     node->v.bbox = *bbox;
    //     node->v.tex = tex;
    //     SLLQueuePush(bbox_draw_list->first, bbox_draw_list->last, node);
    // }
}

} // namespace draw
