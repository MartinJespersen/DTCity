#include "tile_height_compute.hpp"
#include <vulkan/vulkan_core.h>
#include "base/base_core.hpp"
#include "base/container.hpp"
#include "shaders/shader_shared.h"

namespace vulkan
{

VkResult
_tile_height_pipeline_create(VkDevice device, Buffer<const U32> spirv, TileHeightPipeline* pipeline)
{
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = spirv.size * sizeof(U32);
    module_info.pCode = spirv.data;
    VkShaderModule module = {};
    VkResult result = vkCreateShaderModule(device, &module_info, nullptr, &module);
    if (result != VK_SUCCESS)
        return result;
    defer(vkDestroyShaderModule(device, module, nullptr));
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shader::TileHeightPushConstants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    result = vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline->layout);
    if (result != VK_SUCCESS)
        return result;
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = module;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline->layout;
    result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline);
    if (result != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(device, pipeline->layout, nullptr);
        *pipeline = {};
    }
    return result;
}

void
_tile_height_dispatch_record(VkCommandBuffer command, const TileHeightPipeline& pipeline,
                             const shader::TileHeightPushConstants& constants,
                             VkBuffer results, VkBuffer readback)
{
    VkMemoryBarrier2 input_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    input_barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    input_barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    input_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    input_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &input_barrier;
    vkCmdPipelineBarrier2(command, &dependency);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(command, (constants.query_count + 63) / 64, 1, 1);

    VkMemoryBarrier2 output_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    output_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    output_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    output_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    output_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    dependency.pMemoryBarriers = &output_barrier;
    vkCmdPipelineBarrier2(command, &dependency);
    VkBufferCopy copy{0, 0, (VkDeviceSize)constants.query_count * sizeof(shader::TileHeightGpuResult)};
    vkCmdCopyBuffer(command, results, readback, 1, &copy);
    VkMemoryBarrier2 host_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    host_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    dependency.pMemoryBarriers = &host_barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

} // namespace vulkan
