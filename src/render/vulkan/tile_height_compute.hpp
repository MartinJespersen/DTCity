#pragma once

#include <vulkan/vulkan_core.h>
#include "base/base_core.hpp"
#include "base/container.hpp"
#include "shaders/shader_shared.h"

namespace vulkan
{

struct TileHeightPipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

// Kept independent of the application context so headless GPU tests use the
// same pipeline creation and synchronization as the renderer.
VkResult
_tile_height_pipeline_create(VkDevice device, Buffer<const U32> spirv, TileHeightPipeline* pipeline);
void
_tile_height_dispatch_record(VkCommandBuffer command, const TileHeightPipeline& pipeline,
                             const shader::TileHeightPushConstants& constants,
                             VkBuffer results, VkBuffer readback);

} // namespace vulkan
