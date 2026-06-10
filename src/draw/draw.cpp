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
        render::road_intersection_compute_add(node->vertex_buffer_handle, node->index_buffer_handle, node->road_segment_buffer_handle, node->road_segment_node_buffer_handle, node->overlay_option);
    }
}

g_internal bool
draw_road_intersection_compute(render::Handle vertex_buffer_handle, render::Handle index_buffer_handle, render::Handle road_segment_buffer_handle, render::Handle road_segment_node_buffer_handle,
                               U32 overlay_option)
{
    if (!render::is_resource_loaded(vertex_buffer_handle) || !render::is_resource_loaded(index_buffer_handle) || !render::is_resource_loaded(road_segment_buffer_handle) ||
        !render::is_resource_loaded(road_segment_node_buffer_handle))
    {
        return false;
    }

    DrawFrame* frame = draw_frame_get();
    RoadIntersectionNode* node = PushStruct(g_draw_ctx.frame_arena, RoadIntersectionNode);
    node->vertex_buffer_handle = vertex_buffer_handle;
    node->index_buffer_handle = index_buffer_handle;
    node->road_segment_buffer_handle = road_segment_buffer_handle;
    node->road_segment_node_buffer_handle = road_segment_node_buffer_handle;
    node->overlay_option = overlay_option;
    SLLQueuePush(frame->road_intersection_list.first, frame->road_intersection_list.last, node);

    return true;
}

g_internal CarInstanceDrawResult
draw_car_instance_render(render::Handle camera_handle, Buffer<render::MeshHandlePair> meshes, render::Handle tex_handle, render::BufferInfo* instance_buffer_info)
{
    DrawFrame* frame = draw_frame_get();
    U32 align = 16;
    U32 instance_buffer_offset = frame->total_instance_buffer_byte_count + (align - 1);
    instance_buffer_offset -= instance_buffer_offset % align;

    frame->total_instance_buffer_byte_count = Max(frame->total_instance_buffer_byte_count, instance_buffer_offset + instance_buffer_info->buffer.size);

    CarInstanceDrawResult result = {};
    result.render_scheduled = render::car_instance_render_bucket_add(camera_handle, meshes, tex_handle, instance_buffer_info, instance_buffer_offset);
    result.buffer_offset = instance_buffer_offset;

    return result;
}

} // namespace draw
