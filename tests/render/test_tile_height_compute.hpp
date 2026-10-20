#pragma once

#include <vulkan/vulkan_core.h>
#include <vulkan/vk_platform.h>
#include <glm/ext/vector_double3.hpp>
#include <vector>
#include <glm/glm.hpp>
#include "base/base_core.hpp"
#include "render/vulkan/tile_height_compute.hpp"
#include "shaders/shader_shared.h"

namespace height_test
{

struct HeightTestBuffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress address;
    void* mapped;
    VkDeviceSize size;
};

struct HeightTestVertex
{
    U32 prefix;
    float position[3];
    U32 material;
};

struct HeightTestTriangle
{
    glm::dvec3 a;
    glm::dvec3 b;
    glm::dvec3 c;
    U32 mesh_index;
};

struct HeightTest
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    VkCommandPool pool;
    VkQueryPool timestamps;
    U32 timestamp_bits;
    float timestamp_period;
    vulkan::TileHeightPipeline pipeline;
    std::vector<HeightTestBuffer> buffers;
    U32 validation_errors;

    ~HeightTest();
};

static void _height_test_check(VkResult result);
static bool _height_test_initialize(HeightTest& test, U32 device_index);
static U32 _height_test_buffer(HeightTest& test, VkDeviceSize size, VkBufferUsageFlags flags, const void* data);
static shader::TileBvhGpuNode _height_test_bounds(const std::vector<HeightTestTriangle>& triangles, U32 first, U32 count);
static bool _height_test_triangle(const HeightTestTriangle& triangle, glm::dvec3 origin, glm::dvec3 direction, double& closest);
static VKAPI_ATTR VkBool32 VKAPI_CALL _height_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

} // namespace height_test

int main(int argc, char** argv);
