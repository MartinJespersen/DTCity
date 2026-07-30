#pragma once
namespace vulkan
{
struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
};
g_internal Pipeline
agent_instance_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal Pipeline
tile_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal Pipeline
blend_3d_pipeline_create(String8 shader_path);
} // namespace vulkan
