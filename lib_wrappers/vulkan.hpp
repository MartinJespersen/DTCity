#pragma once

// ~mgj: forward declaration
namespace city
{
struct City;
}

namespace wrapper
{
struct Road
{
    VkBuffer inst_buffer;
    VkDeviceMemory inst_memory_buffer;
    VkDeviceSize inst_buffer_size;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
};

static void
RoadPipelineCreate(city::City* city, String8 cwd);
static void
RoadCleanup(city::City* city);

namespace internal
{

struct ShaderModuleInfo
{
    VkPipelineShaderStageCreateInfo info;
    VkDevice device;

    ~ShaderModuleInfo()
    {
        vkDestroyShaderModule(device, info.module, nullptr);
    }
};

static ShaderModuleInfo
ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);
static VkVertexInputBindingDescription
RoadBindingDescriptionGet();
static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena);

} // namespace internal

} // namespace wrapper
