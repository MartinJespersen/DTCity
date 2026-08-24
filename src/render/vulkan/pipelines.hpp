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
// general pipeline functions
g_internal void
pipeline_destroy(Pipeline* draw_ctx);

// agent instancing
g_internal Pipeline
agent_instance_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal void
agent_instance_rendering();

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

// Line rendering
g_internal Pipeline
line_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal void
lines_render(Buffer<render::LineVertex> line_vertices, render::MappedHandle<void> mapped_camera_handle);

} // namespace vulkan
