#pragma once

struct TerrainTransform
{
    glm::highp_mat4 model;
    glm::highp_mat4 view;
    glm::highp_mat4 proj;
};

struct Terrain
{
    TerrainTransform transform;

    Buffer<Vertex> vertices;
    Buffer<U32> indices;

    // vulkan
    VkBuffer* buffer;
    VkDeviceMemory* buffer_memory;
    void** buffer_memory_mapped;

    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;
};

internal void
TerrainAllocations(Arena* arena, Terrain* terrain, U32 frames_in_flight);

internal void
TerrainDescriptorSetLayoutCreate(VkDevice device, Terrain* terrain);

internal void
TerrainUniformBufferCreate(Terrain* terrain, U32 frames_in_flight);

internal void
TerrainGraphicsPipelineCreate(Terrain* terrain);

internal void
TerrainVulkanCleanup(Terrain* terrain, U32 frames_in_flight);
internal void
UpdateTerrainTransform(Terrain* terrain, Vec2F32 screen_res, U32 current_image);

internal void
TerrainDescriptorPoolCreate(Terrain* terrain, U32 frames_in_flight);
internal void
TerrainDescriptorSetCreate(Terrain* terrain, U32 frames_in_flight);

internal void
TerrainRenderPassBegin(Terrain* terrain, U32 image_index, U32 current_frame);

internal void
TerrainInit();

internal VkVertexInputBindingDescription
TerrainBindingDescriptionGet();

internal Buffer<VkVertexInputAttributeDescription>
TerrainAttributeDescriptionGet(Arena* arena);