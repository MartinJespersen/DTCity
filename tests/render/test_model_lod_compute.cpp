// Headless integration test: execute the production compute shader and inspect its indirect commands.
// A CPU Vulkan implementation is sufficient. Blocking waits are confined to this test executable.
#include "test_model_lod_compute.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include "base/container.hpp"
#include "render/vulkan/synchronization.hpp"

ModelComputeTest::~ModelComputeTest()
{
    if (this->device)
    {
        vkDestroyPipeline(this->device, this->pipeline, nullptr);
        vkDestroyShaderModule(this->device, this->shader, nullptr);
        vkDestroyPipelineLayout(this->device, this->pipeline_layout, nullptr);
        vkDestroyDescriptorPool(this->device, this->descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(this->device, this->descriptor_layout, nullptr);
        vkDestroyCommandPool(this->device, this->pool, nullptr);
        for (ModelComputeTestBuffer& buffer : this->buffers)
        {
            if (buffer.mapped) vkUnmapMemory(this->device, buffer.memory);
            vkDestroyBuffer(this->device, buffer.buffer, nullptr);
            vkFreeMemory(this->device, buffer.memory, nullptr);
        }
        vkDestroyDevice(this->device, nullptr);
    }
    if (this->messenger)
    {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(this->instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy(this->instance, this->messenger, nullptr);
    }
    if (this->instance) vkDestroyInstance(this->instance, nullptr);
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) return 1;
    bool legacy_upload_sync = argc == 3 && std::strcmp(argv[2], "--legacy-upload-sync") == 0;
    ModelComputeTest test = {};
    // Enable validation when installed; the test still runs on machines without the layer.
    uint32_t layer_count = 0;
    _model_test_check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    _model_test_check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
    bool validation = false;
    for (const VkLayerProperties& layer : layers)
        validation |= std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    const char* layer_name = "VK_LAYER_KHRONOS_validation";
    const char* extension_name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledLayerCount = validation ? 1 : 0;
    instance_info.ppEnabledLayerNames = &layer_name;
    instance_info.enabledExtensionCount = validation ? 1 : 0;
    instance_info.ppEnabledExtensionNames = &extension_name;
    VkValidationFeatureEnableEXT sync_validation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation_features.enabledValidationFeatureCount = 1;
    validation_features.pEnabledValidationFeatures = &sync_validation;
    instance_info.pNext = validation ? &validation_features : nullptr;
    _model_test_check(vkCreateInstance(&instance_info, nullptr, &test.instance));
    if (validation)
    {
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        info.pfnUserCallback = _model_test_validation;
        info.pUserData = &test;
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(test.instance, "vkCreateDebugUtilsMessengerEXT");
        _model_test_check(create(test.instance, &info, nullptr, &test.messenger));
    }
    uint32_t device_count = 0;
    _model_test_check(vkEnumeratePhysicalDevices(test.instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    _model_test_check(vkEnumeratePhysicalDevices(test.instance, &device_count, devices.data()));
    uint32_t family = 0;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(device, &device_properties);
        if (device_properties.apiVersion < VK_API_VERSION_1_3 ||
            !features.features.shaderInt64 || !features13.synchronization2) continue;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queues.data());
        for (uint32_t i = 0; i < count; ++i)
        {
            if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                test.physical_device = device;
                family = i;
                break;
            }
        }
        if (test.physical_device) break;
    }
    if (!test.physical_device)
    {
        std::puts("SKIP: no Vulkan 1.3 compute device with shaderInt64 and synchronization2");
        return 77;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(test.physical_device, &properties);
    std::printf("Compute test device: %s\n", properties.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures features{};
    features.shaderInt64 = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &features13;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &features;
    _model_test_check(vkCreateDevice(test.physical_device, &device_info, nullptr, &test.device));
    vkGetDeviceQueue(test.device, family, 0, &test.queue);

    // Offsets and sentinels detect writes into neighboring batches and padded workgroup lanes.
    constexpr uint32_t instance_count = 65;
    constexpr uint32_t transform_offset = 3;
    constexpr uint32_t draw_offset = 4;
    constexpr uint32_t command_capacity = 140;
    _model_test_buffer_create(test, 0, sizeof(shader::CameraUniformBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    _model_test_buffer_create(test, 1, MODEL_GEOMETRY_HEADER_WORD_COUNT * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    _model_test_buffer_create(test, 2, MODEL_GEOMETRY_HEADER_WORD_COUNT * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    _model_test_buffer_create(test, 3, (transform_offset + instance_count) * sizeof(shader::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    _model_test_buffer_create(test, 4, command_capacity * sizeof(VkDrawIndexedIndirectCommand),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    auto* camera = (shader::CameraUniformBuffer*)test.buffers[0].mapped;
    *camera = {};
    camera->view = shader::mat4(1);
    camera->proj = shader::mat4(1);
    camera->viewport_dim = shader::vec2(100, 100);
    auto* geometry = (uint32_t*)test.buffers[1].mapped;
    std::memset(geometry, 0, MODEL_GEOMETRY_HEADER_WORD_COUNT * sizeof(uint32_t));
    geometry[MODEL_GEOMETRY_LOD_COUNT_OFFSET] = 3;
    for (uint32_t lod = 0; lod < 3; ++lod)
    {
        uint32_t lod_entry = MODEL_GEOMETRY_LOD_TABLE_OFFSET + lod * MODEL_LOD_ENTRY_WORD_COUNT;
        geometry[lod_entry + MODEL_LOD_DATA_OFFSET] = MODEL_GEOMETRY_HEADER_WORD_COUNT + lod * 30;
        geometry[lod_entry + MODEL_LOD_ELEMENT_COUNT_OFFSET] = 30 - lod * 9;
        float error = float(lod);
        std::memcpy(geometry + lod_entry + MODEL_LOD_ERROR_OFFSET, &error, sizeof(error));
    }
    auto* transforms = (shader::mat4*)test.buffers[3].mapped;
    for (uint32_t i = 0; i < transform_offset + instance_count; ++i)
        transforms[i] = shader::mat4(1);
    // Expected LODs: near=0, medium=1, far=2, behind camera=0, nonuniform scale=0.
    const uint32_t expected_lods[] = {0, 1, 2, 0, 0};
    const float depths[] = {2, 100, 1000, -5, 100};
    for (uint32_t i = 0; i < instance_count; ++i)
    {
        transforms[transform_offset + i][3][2] = -depths[i % 5];
        if (i % 5 == 4) transforms[transform_offset + i][0][0] = 10;
    }

    VkDescriptorSetLayoutBinding bindings[5] = {};
    VkDescriptorBufferInfo buffer_infos[5] = {};
    for (uint32_t i = 0; i < 5; ++i)
    {
        bindings[i] = {i, i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        buffer_infos[i] = {test.buffers[i].buffer, 0, VK_WHOLE_SIZE};
    }
    VkDescriptorSetLayoutCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_info.bindingCount = 5;
    descriptor_info.pBindings = bindings;
    _model_test_check(vkCreateDescriptorSetLayout(test.device, &descriptor_info, nullptr, &test.descriptor_layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    _model_test_check(vkCreateDescriptorPool(test.device, &pool_info, nullptr, &test.descriptor_pool));
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = test.descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &test.descriptor_layout;
    VkDescriptorSet set;
    _model_test_check(vkAllocateDescriptorSets(test.device, &set_info, &set));
    VkWriteDescriptorSet writes[5] = {};
    for (uint32_t i = 0; i < 5; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = bindings[i].descriptorType;
        writes[i].pBufferInfo = &buffer_infos[i];
    }
    vkUpdateDescriptorSets(test.device, 5, writes, 0, nullptr);
    VkPushConstantRange push_range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shader::MeshInstancePushConstants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &test.descriptor_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    _model_test_check(vkCreatePipelineLayout(test.device, &layout_info, nullptr, &test.pipeline_layout));
    std::vector<uint32_t> code;
    {
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        defer(file.close());
        if (!file) return 1;
        size_t bytes = size_t(file.tellg());
        code.resize(bytes / 4);
        file.seekg(0);
        file.read((char*)code.data(), bytes);
        if (!file) return 1;
    }
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = code.size() * 4;
    shader_info.pCode = code.data();
    _model_test_check(vkCreateShaderModule(test.device, &shader_info, nullptr, &test.shader));
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, test.shader, "main", nullptr};
    pipeline_info.layout = test.pipeline_layout;
    _model_test_check(vkCreateComputePipelines(test.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &test.pipeline));
    VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.queueFamilyIndex = family;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    _model_test_check(vkCreateCommandPool(test.device, &command_pool_info, nullptr, &test.pool));
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = test.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer cmd;
    _model_test_check(vkAllocateCommandBuffers(test.device, &allocate, &cmd));
    // Reuse the buffer over frames; test both firstInstance modes, zero error, and empty input.
    for (uint32_t scenario = 0; scenario < 4; ++scenario)
    {
        std::memset(test.buffers[4].mapped, 0xcd, command_capacity * sizeof(VkDrawIndexedIndirectCommand));
        shader::MeshInstancePushConstants constants = {};
        constants.transform_offset = transform_offset;
        constants.draw_offset = draw_offset;
        constants.instance_count = scenario == 3 ? 0 : instance_count;
        constants.lod_error_pixels = scenario == 2 ? 0 : 1;
        constants.indirect_first_instance = scenario == 1;
        _model_test_check(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        _model_test_check(vkBeginCommandBuffer(cmd, &begin));
        if (scenario == 0)
        {
            // Exercise the production upload dependency before the shader reads LOD metadata.
            VkBufferCopy copy{0, 0, MODEL_GEOMETRY_HEADER_WORD_COUNT * sizeof(uint32_t)};
            vkCmdCopyBuffer(cmd, test.buffers[1].buffer, test.buffers[2].buffer, 1, &copy);
            if (!legacy_upload_sync) vulkan::vulkan_upload_barrier_record(cmd);
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, test.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, test.pipeline_layout, 0, 1, &set, 0, nullptr);
        // Artificially limit each dispatch to one group to exercise nonzero first_instance.
        for (uint32_t first : {0u, 64u})
        {
            constants.first_instance = first;
            vkCmdPushConstants(cmd, test.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            vkCmdDispatch(cmd, 1, 1, 1);
        }
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        _model_test_check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        _model_test_check(vkQueueSubmit(test.queue, 1, &submit, VK_NULL_HANDLE));
        _model_test_check(vkQueueWaitIdle(test.queue));
        auto* commands = (VkDrawIndexedIndirectCommand*)test.buffers[4].mapped;
        for (uint32_t i = 0; i < command_capacity; ++i)
        {
            bool active = i >= draw_offset && i < draw_offset + constants.instance_count;
            uint32_t expected[5] = {0xcdcdcdcd, 0xcdcdcdcd, 0xcdcdcdcd, 0xcdcdcdcd, 0xcdcdcdcd};
            if (active)
            {
                uint32_t local = i - draw_offset;
                uint32_t lod = scenario == 2 ? 0 : expected_lods[local % 5];
                expected[0] = 30 - lod * 9;
                expected[1] = 1;
                expected[2] = 24 + lod * 30;
                expected[3] = 0;
                expected[4] = constants.indirect_first_instance ? local : 0;
            }
            if (std::memcmp(&commands[i], expected, sizeof(expected)) != 0)
            {
                std::fprintf(stderr, "FAIL: scenario %u, command %u\n", scenario, i);
                return 1;
            }
        }
    }
    if (test.validation_errors) return 1;
    std::puts("PASS: transfer visibility, GPU LOD selection, instance offsets, split dispatches, partial workgroups, zero error, empty input, frame reuse");
    return 0;
}

static void
_model_test_check(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "Vulkan error: %d\n", result);
        std::exit(1);
    }
}

static void
_model_test_buffer_create(ModelComputeTest& test, unsigned binding, VkDeviceSize size, VkBufferUsageFlags usage)
{
    ModelComputeTestBuffer& buffer = test.buffers[binding];
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    _model_test_check(vkCreateBuffer(test.device, &info, nullptr, &buffer.buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(test.device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(test.physical_device, &properties);
    uint32_t memory_type = properties.memoryTypeCount;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required)
        {
            memory_type = i;
            break;
        }
    }
    if (memory_type == properties.memoryTypeCount) std::exit(77);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    _model_test_check(vkAllocateMemory(test.device, &allocate, nullptr, &buffer.memory));
    _model_test_check(vkBindBufferMemory(test.device, buffer.buffer, buffer.memory, 0));
    _model_test_check(vkMapMemory(test.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
_model_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                       const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    auto* test = (ModelComputeTest*)user;
    ++test->validation_errors;
    std::fprintf(stderr, "%s\n", data->pMessage);
    return VK_FALSE;
}
