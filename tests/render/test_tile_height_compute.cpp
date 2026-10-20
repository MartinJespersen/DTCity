// Headless execution of the production height shader and dispatch/barrier helper.
// Blocking waits belong only to this test executable, never the application frame loop.
#include "test_tile_height_compute.hpp"
#include "base/container.hpp"
#include "shaders/shader_shared.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan_core.h>
#include <vulkan/vk_platform.h>
#include "base/base_core.hpp"
#include "render/vulkan/tile_height_compute.hpp"
#include <cstdint>
#include <ios>
#include <ratio>
#include <vector>
#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/geometric.hpp>
#include <glm/common.hpp>
#include <glm/matrix.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace height_test
{

HeightTest::~HeightTest()
{
    if (this->device)
    {
        vkDeviceWaitIdle(this->device);
        vkDestroyPipeline(this->device, this->pipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(this->device, this->pipeline.layout, nullptr);
        vkDestroyQueryPool(this->device, this->timestamps, nullptr);
        vkDestroyCommandPool(this->device, this->pool, nullptr);
        for (const HeightTestBuffer& buffer : this->buffers)
        {
            vkUnmapMemory(this->device, buffer.memory);
            vkDestroyBuffer(this->device, buffer.buffer, nullptr);
            vkFreeMemory(this->device, buffer.memory, nullptr);
        }
        vkDestroyDevice(this->device, nullptr);
    }
    if (this->messenger)
    {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(this->instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroy(this->instance, this->messenger, nullptr);
    }
    if (this->instance)
        vkDestroyInstance(this->instance, nullptr);
}

} // namespace height_test

int main(int argc, char** argv)
{
    using namespace height_test;
    if (argc < 2 || argc > 3)
        return 1;
    HeightTest test = {};
    if (!_height_test_initialize(test, argc == 3 ? (U32)std::strtoul(argv[2], nullptr, 10) : 0))
        return 77;
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file)
        return 1;
    size_t byte_count = (size_t)file.tellg();
    std::vector<U32> spirv(byte_count / sizeof(U32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(spirv.data()), (std::streamsize)byte_count);
    _height_test_check(vulkan::_tile_height_pipeline_create(test.device, {spirv.data(), spirv.size()}, &test.pipeline));

    constexpr U32 mesh_count = 7;
    constexpr U32 query_count = 4097; // Includes a partial compute workgroup.
    constexpr U32 padded_count = (query_count + 63) / 64 * 64;
    std::vector<shader::TileBvhGpuEntry> entries(mesh_count);
    std::vector<shader::TileBvhGpuMeshAddresses> addresses(mesh_count);
    std::vector<HeightTestTriangle> world_triangles;
    for (U32 mesh = 0; mesh < mesh_count; ++mesh)
    {
        std::vector<HeightTestVertex> vertices(1); // Unreferenced far vertex.
        vertices[0] = {0, {99999, 99999, 99999}, 0};
        std::vector<U32> indices(1, 0xbaadf00d); // Exercise nonzero index-buffer offsets.
        std::vector<HeightTestTriangle> triangles;
        glm::mat4 transform(1.0f);
        U32 grid_row = mesh / 3;
        transform = glm::translate(transform, glm::vec3((float)(mesh % 3) * 3, (float)grid_row * 3, (float)mesh * 2));
        transform = glm::rotate(transform, (float)mesh * 0.13f, glm::vec3(0, 0, 1));
        transform = glm::scale(transform, glm::vec3(1.0f + mesh * 0.1f, 0.6f, 1.5f + mesh * 0.3f));
        glm::mat4 inverse = glm::inverse(transform);
        for (U32 row = 0; row < 3; ++row)
            for (U32 column = 0; column < 4; ++column)
                entries[mesh].city_to_mesh_rows[row][column] = inverse[column][row];
        for (U32 y = 0; y < 10; ++y)
            for (U32 x = 0; x < 10; ++x)
                for (U32 side = 0; side < 2; ++side)
                {
                    glm::vec3 corners[4] = {{(float)x, (float)y, 0}, {(float)x + 1, (float)y, 0},
                                           {(float)x + 1, (float)y + 1, 0}, {(float)x, (float)y + 1, 0}};
                    U32 corner_indices[3] = {0, side ? 2u : 1u, side ? 3u : 2u};
                    if ((x + y) & 1)
                        std::swap(corner_indices[0], corner_indices[2]); // Double-sided queries.
                    HeightTestTriangle local = {};
                    glm::dvec3* points[3] = {&local.a, &local.b, &local.c};
                    for (U32 corner = 0; corner < 3; ++corner)
                    {
                        glm::vec3 position = corners[corner_indices[corner]];
                        indices.push_back((U32)vertices.size());
                        vertices.push_back({0xdeadbeef, {position.x, position.y, position.z}, 7});
                        *points[corner] = glm::dvec3(position);
                    }
                    triangles.push_back(local);
                    world_triangles.push_back({glm::dvec3(transform * glm::vec4(local.a, 1)),
                                               glm::dvec3(transform * glm::vec4(local.b, 1)),
                                               glm::dvec3(transform * glm::vec4(local.c, 1)), mesh});
                }
        // A root and two leaf ranges exercise inner traversal. The production
        // median builder's deeper trees are covered by the CPU BVH tests.
        shader::TileBvhGpuNode nodes[3] = {};
        nodes[0] = _height_test_bounds(triangles, 0, (U32)triangles.size());
        nodes[0].count = 0;
        nodes[0].first = 1;
        nodes[1] = _height_test_bounds(triangles, 0, (U32)triangles.size() / 2);
        nodes[2] = _height_test_bounds(triangles, (U32)triangles.size() / 2, (U32)triangles.size() / 2);
        U32 vertex_buffer = _height_test_buffer(test, vertices.size() * sizeof(HeightTestVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vertices.data());
        U32 index_buffer = _height_test_buffer(test, indices.size() * sizeof(U32), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indices.data());
        U32 node_buffer = _height_test_buffer(test, sizeof(nodes), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nodes);
        addresses[mesh] = {test.buffers[vertex_buffer].address, test.buffers[index_buffer].address, test.buffers[node_buffer].address, 0};
        entries[mesh].mesh_buffer_index = mesh;
        entries[mesh].vertex_position_offset_bytes = offsetof(HeightTestVertex, position);
        entries[mesh].vertex_stride_bytes = sizeof(HeightTestVertex);
        entries[mesh].index_offset_bytes = sizeof(U32);
        entries[mesh].entry_handle_idx = 13 + mesh * 17;
        entries[mesh].entry_handle_gen_id = 7 + mesh;
    }
    shader::TileBvhGpuNode outer[3] = {};
    // All entries overlap. Visiting the first leaf alone cannot find every closest hit.
    outer[0] = _height_test_bounds(world_triangles, 0, (U32)world_triangles.size());
    outer[0].count = 0;
    outer[0].first = 1;
    outer[1] = outer[0]; outer[1].first = 0; outer[1].count = 3;
    outer[2] = outer[0]; outer[2].first = 3; outer[2].count = mesh_count - 3;
    U32 outer_buffer = _height_test_buffer(test, sizeof(outer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outer);
    // BVH sorting changes entry order while buffer-address indices stay stable.
    std::reverse(entries.begin(), entries.end());
    U32 entry_buffer = _height_test_buffer(test, entries.size() * sizeof(entries[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, entries.data());
    U32 address_buffer = _height_test_buffer(test, addresses.size() * sizeof(addresses[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, addresses.data());
    std::vector<shader::TileHeightGpuQuery> queries(padded_count);
    for (U32 index = 0; index < query_count; ++index)
    {
        glm::vec3 origin((float)(index % 31) - 6.0f + 0.27f, (float)((index / 31) % 23) - 6.0f + 0.31f, 100.0f);
        glm::vec3 direction(0, 0, -1);
        if (index % 7 == 1) direction = glm::normalize(glm::vec3(0.1f, -0.03f, -1));
        if (index % 7 == 2) direction = glm::vec3(1, 0, 0); // Parallel and misses.
        if (index % 7 == 3) origin.z = -20; // Surface behind the ray.
        if (index % 7 == 4) origin.z = 3; // Origin inside outer bounds.
        if (index % 7 == 5) origin = glm::vec3(0, 0, 100); // Shared edge/vertex hits.
        for (U32 axis = 0; axis < 3; ++axis)
        {
            queries[index].origin[axis] = origin[axis];
            queries[index].direction[axis] = direction[axis];
        }
    }
    U32 query_buffer = _height_test_buffer(test, queries.size() * sizeof(queries[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, queries.data());
    U32 result_buffers[3] = {};
    U32 readback_buffers[3] = {};
    VkCommandBuffer commands[3] = {};
    VkFence fences[3] = {};
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = test.pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 3;
    _height_test_check(vkAllocateCommandBuffers(test.device, &command_info, commands));

    // Submit a replacement while the first version can still be in flight.
    // Keep both versions' buffers alive until their fences have completed.
    auto replacement_entries = entries;
    for (auto& entry : replacement_entries)
        for (U32 row = 0; row < 3; ++row)
            entry.city_to_mesh_rows[row][3] -= entry.city_to_mesh_rows[row][2] * 10.0f;
    U32 replacement_buffer = _height_test_buffer(test, replacement_entries.size() * sizeof(entries[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, replacement_entries.data());
    shader::TileBvhGpuNode shifted_outer[3];
    std::memcpy(shifted_outer, outer, sizeof(shifted_outer));
    for (auto& node : shifted_outer) { node.bounds_min[2] += 10; node.bounds_max[2] += 10; }
    U32 shifted_buffer = _height_test_buffer(test, sizeof(shifted_outer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, shifted_outer);
    auto started = std::chrono::steady_clock::now();
    for (U32 scenario = 0; scenario < 3; ++scenario)
    {
        VkDeviceSize bytes = padded_count * sizeof(shader::TileHeightGpuResult);
        result_buffers[scenario] = _height_test_buffer(test, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr);
        readback_buffers[scenario] = _height_test_buffer(test, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
        std::memset(test.buffers[result_buffers[scenario]].mapped, 0xcd, (size_t)bytes);
        std::memset(test.buffers[readback_buffers[scenario]].mapped, 0xcd, (size_t)bytes);
        shader::TileHeightPushConstants constants = {};
        constants.nodes = scenario == 1 ? test.buffers[shifted_buffer].address : test.buffers[outer_buffer].address;
        constants.entries = scenario == 1 ? test.buffers[replacement_buffer].address : test.buffers[entry_buffer].address;
        constants.meshes = test.buffers[address_buffer].address;
        constants.queries = test.buffers[query_buffer].address;
        constants.results = test.buffers[result_buffers[scenario]].address;
        constants.node_count = scenario == 2 ? 0 : 3;
        constants.query_count = query_count;
        if (scenario == 2) { constants.nodes = 0; constants.entries = 0; constants.meshes = 0; }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        _height_test_check(vkBeginCommandBuffer(commands[scenario], &begin));
        if (test.timestamps && scenario == 0) vkCmdResetQueryPool(commands[scenario], test.timestamps, 0, 6);
        if (test.timestamps) vkCmdWriteTimestamp2(commands[scenario], VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, test.timestamps, scenario * 2);
        vulkan::_tile_height_dispatch_record(commands[scenario], test.pipeline, constants,
            test.buffers[result_buffers[scenario]].buffer, test.buffers[readback_buffers[scenario]].buffer);
        // Copy the padded output lanes too: a guard only in the readback buffer
        // would not detect accidental shader writes beyond query_count.
        VkBufferCopy tail{(VkDeviceSize)query_count * sizeof(shader::TileHeightGpuResult),
                          (VkDeviceSize)query_count * sizeof(shader::TileHeightGpuResult),
                          (padded_count - query_count) * sizeof(shader::TileHeightGpuResult)};
        vkCmdCopyBuffer(commands[scenario], test.buffers[result_buffers[scenario]].buffer,
                        test.buffers[readback_buffers[scenario]].buffer, 1, &tail);
        VkMemoryBarrier2 guard_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        guard_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        guard_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        guard_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        guard_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo guard_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        guard_dependency.memoryBarrierCount = 1;
        guard_dependency.pMemoryBarriers = &guard_barrier;
        vkCmdPipelineBarrier2(commands[scenario], &guard_dependency);
        if (test.timestamps) vkCmdWriteTimestamp2(commands[scenario], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, test.timestamps, scenario * 2 + 1);
        _height_test_check(vkEndCommandBuffer(commands[scenario]));
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        _height_test_check(vkCreateFence(test.device, &fence_info, nullptr, &fences[scenario]));
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = commands[scenario];
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command;
        _height_test_check(vkQueueSubmit2(test.queue, 1, &submit, fences[scenario]));
    }
    _height_test_check(vkWaitForFences(test.device, 3, fences, VK_TRUE, UINT64_MAX));
    double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    auto cpu_started = std::chrono::steady_clock::now();
    U32 failures = 0;
    for (U32 scenario = 0; scenario < 3; ++scenario)
    {
        auto* results = static_cast<const shader::TileHeightGpuResult*>(test.buffers[readback_buffers[scenario]].mapped);
        for (U32 query = 0; query < query_count; ++query)
        {
            const auto& ray = queries[query];
            glm::dvec3 origin(ray.origin[0], ray.origin[1], ray.origin[2]);
            glm::dvec3 direction(ray.direction[0], ray.direction[1], ray.direction[2]);
            if (scenario == 1) origin.z -= 10;
            double closest = std::numeric_limits<double>::max();
            bool hit = false;
            U32 closest_mesh = 0;
            if (scenario != 2)
                for (const auto& triangle : world_triangles)
                    if (_height_test_triangle(triangle, origin, direction, closest))
                    {
                        hit = true;
                        closest_mesh = triangle.mesh_index;
                    }
            U32 expected_entry_idx = hit ? 13 + closest_mesh * 17 : 0;
            U32 expected_entry_gen_id = hit ? 7 + closest_mesh : 0;
            if (results[query].hit != (U32)hit || (hit && std::abs(results[query].hit_distance - closest) > 0.002) ||
                results[query].entry_handle_idx != expected_entry_idx ||
                results[query].entry_handle_gen_id != expected_entry_gen_id)
            {
                if (failures++ < 8)
                    std::fprintf(stderr, "FAIL scenario=%u query=%u: GPU hit=%u distance=%g entry=%u:%u; CPU hit=%u distance=%g entry=%u:%u\n",
                        scenario, query, results[query].hit, results[query].hit_distance,
                        results[query].entry_handle_idx, results[query].entry_handle_gen_id,
                        (U32)hit, closest, expected_entry_idx, expected_entry_gen_id);
            }
        }
        const U32* sentinel = reinterpret_cast<const U32*>(&results[query_count]);
        for (U32 word = 0; word < (padded_count - query_count) * sizeof(shader::TileHeightGpuResult) / sizeof(U32); ++word)
            if (sentinel[word] != 0xcdcdcdcd) ++failures;
        vkDestroyFence(test.device, fences[scenario], nullptr);
    }
    double cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpu_started).count();
    if (test.timestamps)
    {
        U64 stamps[6] = {};
        _height_test_check(vkGetQueryPoolResults(test.device, test.timestamps, 0, 6, sizeof(stamps), stamps, sizeof(U64), VK_QUERY_RESULT_64_BIT));
        U64 mask = test.timestamp_bits == 64 ? ~U64(0) : (U64(1) << test.timestamp_bits) - 1;
        for (U32 scenario = 0; scenario < 3; ++scenario)
            std::printf("GPU dispatch+copy scenario %u: %.3f ms\n", scenario,
                (double)((stamps[(U64)scenario * 2 + 1] - stamps[(U64)scenario * 2]) & mask) * test.timestamp_period / 1e6);
    }
    std::printf("%u rays x 3 scenarios; submission/readback %.3f ms; CPU brute force %.3f ms; validation errors %u\n",
        query_count, elapsed_ms, cpu_ms, test.validation_errors);
    if (failures || test.validation_errors)
        return 1;
    std::puts("PASS: closest hits, misses, overlaps, transforms, offsets, parallel rays, edges, in-flight replacement and empty BVH");
    return 0;
}

namespace height_test
{

// Private test helpers.
void _height_test_check(VkResult result)
{
    if (result != VK_SUCCESS) { std::fprintf(stderr, "Vulkan error %d\n", result); std::exit(1); }
}

bool _height_test_initialize(HeightTest& test, U32 device_index)
{
    U32 layer_count = 0;
    _height_test_check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    _height_test_check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
    bool validation = false;
    for (const auto& layer : layers) validation |= std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.apiVersion = VK_API_VERSION_1_3;
    VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation_features.enabledValidationFeatureCount = 1; validation_features.pEnabledValidationFeatures = &sync;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &application;
    instance.enabledLayerCount = validation ? 1 : 0; instance.ppEnabledLayerNames = &layer;
    instance.enabledExtensionCount = validation ? 1 : 0; instance.ppEnabledExtensionNames = &extension;
    instance.pNext = validation ? &validation_features : nullptr;
    VkResult result = vkCreateInstance(&instance, nullptr, &test.instance);
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER) { std::puts("SKIP: no Vulkan driver"); return false; }
    _height_test_check(result);
    if (validation)
    {
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        info.pfnUserCallback = _height_test_validation; info.pUserData = &test;
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(test.instance, "vkCreateDebugUtilsMessengerEXT"));
        _height_test_check(create(test.instance, &info, nullptr, &test.messenger));
    }
    U32 count = 0; _height_test_check(vkEnumeratePhysicalDevices(test.instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count); _height_test_check(vkEnumeratePhysicalDevices(test.instance, &count, devices.data()));
    if (device_index >= count) return false;
    U32 family = 0;
    for (U32 candidate = device_index; candidate < count; ++candidate)
    {
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features12; features12.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(devices[candidate], &features);
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(devices[candidate], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 || !features.features.shaderInt64 || !features12.bufferDeviceAddress || !features13.synchronization2)
            continue;
        U32 queue_count = 0; vkGetPhysicalDeviceQueueFamilyProperties(devices[candidate], &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count); vkGetPhysicalDeviceQueueFamilyProperties(devices[candidate], &queue_count, queues.data());
        for (U32 index = 0; index < queue_count; ++index)
            if (queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                test.physical = devices[candidate]; family = index;
                test.timestamp_bits = queues[index].timestampValidBits; test.timestamp_period = properties.limits.timestampPeriod;
                std::printf("Height compute device: %s; synchronization validation %s\n", properties.deviceName, validation ? "enabled" : "unavailable");
                break;
            }
        if (test.physical) break;
    }
    if (!test.physical) { std::puts("SKIP: Vulkan 1.3 compute/BDA/shaderInt64 unavailable"); return false; }
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; features12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; features13.synchronization2 = VK_TRUE;
    features12.pNext = &features13;
    VkPhysicalDeviceFeatures features{}; features.shaderInt64 = VK_TRUE;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.pNext = &features12; device.pEnabledFeatures = &features;
    device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
    _height_test_check(vkCreateDevice(test.physical, &device, nullptr, &test.device));
    vkGetDeviceQueue(test.device, family, 0, &test.queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = family;
    _height_test_check(vkCreateCommandPool(test.device, &pool, nullptr, &test.pool));
    if (test.timestamp_bits)
    {
        VkQueryPoolCreateInfo timestamps{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        timestamps.queryType = VK_QUERY_TYPE_TIMESTAMP; timestamps.queryCount = 6;
        _height_test_check(vkCreateQueryPool(test.device, &timestamps, nullptr, &test.timestamps));
    }
    return true;
}

U32 _height_test_buffer(HeightTest& test, VkDeviceSize size, VkBufferUsageFlags flags, const void* data)
{
    HeightTestBuffer buffer = {}; buffer.size = size;
    bool address = (flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = size;
    info.usage = flags | (address ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0);
    _height_test_check(vkCreateBuffer(test.device, &info, nullptr, &buffer.buffer));
    VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(test.device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(test.physical, &properties);
    U32 type = 0;
    VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < properties.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) || (properties.memoryTypes[type].propertyFlags & required) != required)) ++type;
    if (type == properties.memoryTypeCount) { std::fputs("No host-visible coherent memory\n", stderr); std::exit(77); }
    VkMemoryAllocateFlagsInfo allocation_flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; allocation_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type; allocation.pNext = address ? &allocation_flags : nullptr;
    _height_test_check(vkAllocateMemory(test.device, &allocation, nullptr, &buffer.memory));
    _height_test_check(vkBindBufferMemory(test.device, buffer.buffer, buffer.memory, 0));
    _height_test_check(vkMapMemory(test.device, buffer.memory, 0, size, 0, &buffer.mapped));
    if (data) std::memcpy(buffer.mapped, data, (size_t)size);
    if (address)
    {
        VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address_info.buffer = buffer.buffer;
        buffer.address = vkGetBufferDeviceAddress(test.device, &address_info);
    }
    U32 index = (U32)test.buffers.size(); test.buffers.push_back(buffer); return index;
}

shader::TileBvhGpuNode _height_test_bounds(const std::vector<HeightTestTriangle>& triangles, U32 first, U32 count)
{
    glm::dvec3 minimum = triangles[first].a, maximum = minimum;
    for (U32 index = first; index < first + count; ++index)
        for (glm::dvec3 point : {triangles[index].a, triangles[index].b, triangles[index].c})
        { minimum = glm::min(minimum, point); maximum = glm::max(maximum, point); }
    shader::TileBvhGpuNode node = {}; node.first = first; node.count = count;
    for (U32 axis = 0; axis < 3; ++axis) { node.bounds_min[axis] = (float)minimum[axis]; node.bounds_max[axis] = (float)maximum[axis]; }
    return node;
}

bool _height_test_triangle(const HeightTestTriangle& triangle, glm::dvec3 origin, glm::dvec3 direction, double& closest)
{
    glm::dvec3 a = triangle.b - triangle.a, b = triangle.c - triangle.a;
    glm::dvec3 p = glm::cross(direction, b);
    double determinant = glm::dot(a, p);
    if (std::abs(determinant) <= 1e-12) return false;
    glm::dvec3 offset = origin - triangle.a, q = glm::cross(offset, a);
    double u = glm::dot(offset, p) / determinant, v = glm::dot(direction, q) / determinant;
    double distance = glm::dot(b, q) / determinant;
    if (u < -1e-6 || v < -1e-6 || u + v > 1.000001 || distance < 0 || distance >= closest) return false;
    closest = distance; return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL _height_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)severity; (void)type;
    ++static_cast<HeightTest*>(user)->validation_errors;
    std::fprintf(stderr, "Validation: %s\n", data->pMessage);
    return VK_FALSE;
}

} // namespace height_test
