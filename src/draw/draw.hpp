#pragma once

namespace draw
{

struct TilePipelineNode
{
    TilePipelineNode* next;
    render::TilePipelineData pipeline_input;
    render::Handle colormap_handle;
};

struct TilePipelineList
{
    TilePipelineNode* first;
    TilePipelineNode* last;
};

struct Blend3DNode
{
    Blend3DNode* next;
    render::Blend3DPipelineData pipeline_input;
};

struct Blend3DList
{
    Blend3DNode* first;
    Blend3DNode* last;
};

struct RoadIntersectionNode
{
    RoadIntersectionNode* next;
    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    render::Handle road_segment_buffer_handle;
    render::Handle road_segment_node_buffer_handle;
    U32 overlay_option;
};

struct RoadIntersectionList
{
    RoadIntersectionNode* first;
    RoadIntersectionNode* last;
};

template <typename T>
struct MappedHandle
{
    T* data;
    render::Handle handle;
};

struct DrawFrame
{
    U32 total_instance_buffer_byte_count;
    Blend3DList blend_3d_list;
    RoadIntersectionList road_intersection_list;
};

struct CarInstanceDrawResult
{
    bool render_scheduled;
    U32 buffer_offset;
};

struct Draw
{
    // reset every frame
    Arena* frame_arena;
    DrawFrame* frame;
};

g_internal void
draw_init();
g_internal void
draw_release();
g_internal void
draw_new_frame();
g_internal Arena*
draw_frame_arena_get();
g_internal DrawFrame*
draw_frame_get();
g_internal void
draw_flush();

g_internal void
draw_blend_3d(render::Blend3DPipelineData pipeline_input);
g_internal bool
draw_road_intersection_compute(render::Handle vertex_buffer_handle, render::Handle index_buffer_handle, render::Handle road_segment_buffer_handle, render::Handle road_segment_node_buffer_handle,
                               U32 overlay_option);
g_internal CarInstanceDrawResult
draw_car_instance_render(render::MappedHandle<void> camera_handle, Buffer<render::MeshHandlePair> meshes, Buffer<render::Handle> texture_handles, render::BufferInfo* instance_buffer_info);

} // namespace draw
