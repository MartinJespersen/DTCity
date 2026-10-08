#pragma once

#include "render/render.hpp"

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

struct PrimitiveInstanceNode
{
    PrimitiveInstanceNode* next;
    ChunkList<glm::vec3>* location_chunk_list;
    F32 scale_factor;
    render::MeshHandle mesh_handle;
};

struct PrimitiveInstanceList
{
    PrimitiveInstanceNode* first;
    PrimitiveInstanceNode* last;
    U64 count;
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
    ChunkList<render::LineVertex>* line_vertex_chunk_list;
    PrimitiveInstanceList primitive_instance_list;
    ChunkList<render::MeshInstanceBatch>* mesh_instance_batches;
    ArrayResourcePoolHandle camera_resource_handle;
};

struct Draw
{
    // reset every frame
    Arena* frame_arena;
    DrawFrame* frame;
};

void
draw_init();
void
draw_release();
void
draw_new_frame();
Arena*
draw_frame_arena_get();
DrawFrame*
draw_frame_get();

void
draw_camera_set(ArrayResourcePoolHandle camera_resource_handle);
void
draw_line(render::Line& line);
void
primitive_draw(glm::vec3 location, F32 scale_factor, render::MeshHandle mesh_handle);

g_internal void
draw_mesh_instances(render::MeshletMeshHandle mesh_handle, Buffer<render::Transform> transforms);

g_internal void
draw_blend_3d(render::Blend3DPipelineData pipeline_input);

} // namespace draw
