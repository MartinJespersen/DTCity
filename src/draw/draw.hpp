#pragma once

namespace draw
{

struct Model3DNode
{
    Model3DNode* next;
    render::Model3DPipelineData pipeline_input;
    render::Handle colormap_handle;
};

struct Model3DNodeList
{
    Model3DNode* first;
    Model3DNode* last;
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
    render::Handle storage_buffer_handle;
    render::Handle index_buffer_handle;
    render::Handle road_segment_buffer_handle;
    render::Handle road_segment_node_buffer_handle;
    render::Handle road_segment_handle;
    U32 overlay_option;
};

struct RoadIntersectionList
{
    RoadIntersectionNode* first;
    RoadIntersectionNode* last;
};

struct CarInstanceComputeNode
{
    CarInstanceComputeNode* next;
    render::BufferInfo instance_buffer_info;
    render::Handle tile_vertex_buffer_handle;
    render::Handle tile_index_buffer_handle;
    F32 car_center_to_road_offset;
    U32 instance_buffer_offset;
};

struct CarInstanceComputeNodeList
{
    CarInstanceComputeNode* first;
    CarInstanceComputeNode* last;
};

struct CarInstanceCompute
{
    CarInstanceComputeNodeList list;
};

struct CarInstanceRenderNode
{
    CarInstanceRenderNode* next;
    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    render::Handle texture_handle;
    render::BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
};

struct CarInstanceRenderNodeList
{
    CarInstanceRenderNode* first;
    CarInstanceRenderNode* last;
};

struct CarInstanceRender
{
    CarInstanceRenderNodeList list;
    U32 total_instance_buffer_byte_count;
};

struct CarInstanceRenderAddResult
{
    B32 queued;
    U32 instance_buffer_offset;
};

struct DrawFrame
{
    Model3DNodeList model_3D_list;
    CarInstanceCompute car_instance_compute_list;
    CarInstanceRender car_instance_render_list;
    Blend3DList blend_3d_list;
    RoadIntersectionList road_intersection_list;
    LinkedList<render::BBoxDraw> bbox_list;
};

struct Draw
{
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
draw_model_3d(render::Model3DPipelineData pipeline_input, render::Handle colormap_handle);
g_internal void
draw_blend_3d(render::Blend3DPipelineData pipeline_input);
g_internal bool
draw_road_intersection_compute(render::Handle storage_buffer_handle, render::Handle index_buffer_handle, render::Handle road_segment_buffer_handle, render::Handle road_segment_node_buffer_handle,
                               render::Handle road_segment_handle, U32 overlay_option);
g_internal CarInstanceRenderAddResult
draw_car_instance_render(render::Handle vertex_buffer_handle, render::Handle index_buffer_handle, render::Handle tex_handle, render::BufferInfo* instance_buffer_info);
g_internal void
draw_car_instance_compute(render::BufferInfo* instance_buffer_info, render::Handle tile_vertex_buffer_handle, render::Handle tile_index_buffer_handle, F32 car_center_to_road_offset,
                          U32 instance_buffer_offset);

g_internal void
draw_bbox_3d_draw(Rng2F64 bbox);

} // namespace draw
