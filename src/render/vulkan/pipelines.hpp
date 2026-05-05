#pragma once
namespace vulkan
{
struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
};
g_internal Pipeline
car_instance_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal Pipeline
model_3d_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal Pipeline
blend_3d_pipeline_create(String8 shader_path);
g_internal Pipeline
road_intersection_pipeline_create(String8 shader_path);
g_internal Pipeline
car_instance_compute_pipeline_create(String8 shader_path);
g_internal Pipeline
bbox_pipeline(vulkan::Context* vk_ctx, String8 shader_path);
} // namespace vulkan
