#pragma once

#include "shaders/shader_shared.h"

#include "core_inc.hpp"
#include "render/render.hpp"
namespace vulkan
{
struct Context;

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

using MeshInstancePushConstants = shader::MeshInstancePushConstants;
struct ModelIndirectBatch
{
    VkDescriptorBufferInfo buffers[5];
    MeshInstancePushConstants constants;
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
tile_rendering(render::MappedHandle<void> mapped_camera_handle);

// Primitive rendering
g_internal Pipeline
primitive_pipeline_create(String8 shader_path);
g_internal void
primitive_rendering(Buffer<render::PrimitiveInstance> primitive_instances, render::MappedHandle<void> camera_handle);

g_internal Pipeline
model_instance_pipeline_create(String8 shader_path);
g_internal void
model_instance_rendering(Buffer<ModelIndirectBatch> batches);

// EXT mesh shaders: PrimitiveVertex storage, packed meshlets, per-instance transforms.
g_internal void
model_instance_mesh_rendering(Buffer<render::AgentMeshInstanceBatch> mesh_batches, render::MappedHandle<void> camera_handle);

// Line rendering
g_internal Pipeline
line_pipeline_create(Context* vk_ctx, String8 shader_path);
g_internal void
lines_render(Buffer<render::LineVertex> line_vertices, render::MappedHandle<void> mapped_camera_handle);

// Record compute before entering dynamic rendering.
g_internal Buffer<ModelIndirectBatch>
_vulkan_model_compute_prepare(Buffer<render::AgentMeshInstanceBatch> mesh_batches,
                              render::MappedHandle<void> camera_handle);

} // namespace vulkan
