#pragma once
namespace vulkan
{
enum WriteType
{
    WriteType_Color,
    WriteType_Depth,
    WriteType_Count
};
struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
};

struct MeshInstancePushConstants
{
    U32 first_meshlet;
    U32 first_instance;
    U32 transform_offset;
    U32 vertex_stride;
    U32 position_offset;
    U32 normal_offset;
    U32 color_offset;
    U32 uv_offset;
    U32 texture_index;
    U32 textured;
    F32 lod_error_pixels;
};
// general pipeline functions
g_internal void
pipeline_destroy(Pipeline* draw_ctx);

// blend 3d rendering
g_internal Pipeline
blend_3d_pipeline_create(String8 shader_path);
g_internal void
blend_3d_rendering();

// tile rendering
g_internal Pipeline
tile_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal void
tile_rendering();

// Primitive rendering
g_internal Pipeline
primitive_pipeline_create(String8 shader_path);
g_internal void
primitive_rendering(Buffer<render::PrimitiveInstance> primitive_instances, render::MappedHandle<void> camera_handle);

// EXT mesh shaders: PrimitiveVertex storage, packed meshlets, per-instance transforms.
g_internal Pipeline
mesh_instance_pipeline_create(String8 shader_path);
g_internal void
mesh_instance_rendering(Buffer<render::MeshInstanceBatch> mesh_batches, render::MappedHandle<void> camera_handle);

// Line rendering
g_internal Pipeline
line_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal void
lines_render(Buffer<render::LineVertex> line_vertices, render::MappedHandle<void> mapped_camera_handle);

} // namespace vulkan
