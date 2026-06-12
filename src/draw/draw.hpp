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

// when a task is completed, the fence count is decremented
enum class DrawType
{
    None,
    BBox3d
};

struct AsyncDrawFence
{
    std::atomic<U32> count;
    DrawType type;
    union
    {
        render::BBoxDraw bbox_draw;
    };
};

struct AsyncDrawFenceNode
{
    AsyncDrawFenceNode* next;
    AsyncDrawFence v;
};
struct AsyncDrawFenceList
{
    AsyncDrawFenceNode* first;
    AsyncDrawFenceNode* last;
};

struct AsyncFenceHandle
{
    AsyncDrawFence* ptr;
    U64 gen_id;
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

    // persisted across frames
    AsyncDrawFenceList async_draw_list;
    AsyncDrawFenceNode* async_draw_free_list;
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
draw_car_instance_render(render::MappedHandle<void> camera_handle, Buffer<render::MeshHandlePair> meshes, render::Handle tex_handle, render::BufferInfo* instance_buffer_info);

} // namespace draw
