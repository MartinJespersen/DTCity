#pragma once
#include <vulkan/vulkan.h>
#include "shaders/shader_shared.h"

struct ModelComputeTestBuffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
};

struct ModelComputeTest
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkQueue queue;
    VkCommandPool pool;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkPipelineLayout pipeline_layout;
    VkShaderModule shader;
    VkPipeline pipeline;
    ModelComputeTestBuffer buffers[5];
    unsigned validation_errors;

    ~ModelComputeTest();
};

int main(int argc, char** argv);

static void
_model_test_check(VkResult result);
static void
_model_test_buffer_create(ModelComputeTest& test, unsigned binding, VkDeviceSize size, VkBufferUsageFlags usage);
static VKAPI_ATTR VkBool32 VKAPI_CALL
_model_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                       const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
