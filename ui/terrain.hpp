#pragma once

struct TerrainUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    float displacement_factor;
    float tessellation_factor;
    float patch_size;
    glm::vec2 viewport_dim;
    float tessellated_edge_size;
};

struct Terrain
{
    TerrainUniformBuffer uniform_buffer;

    Buffer<Vertex> vertices;
    Buffer<U32> indices;
    U32 patch_size;

    // vulkan
    VkBuffer* buffer;
    VkDeviceMemory* buffer_memory;
    void** buffer_memory_mapped;

    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;

    VkImage vk_texture_image;
    VkDeviceMemory vk_texture_image_memory;
    VkImageView vk_texture_image_view;
    VkSampler vk_texture_sampler;
    U32 vk_mip_levels;
    VkFormat vk_texture_blit_format;
};

internal void
TerrainAllocations(Arena* arena, Terrain* terrain, U32 frames_in_flight);

internal void
TerrainDescriptorSetLayoutCreate(VkDevice device, Terrain* terrain);

internal void
TerrainUniformBufferCreate(Terrain* terrain, U32 frames_in_flight);

internal void
TerrainGraphicsPipelineCreate(Terrain* terrain, const char* cwd);

internal void
TerrainVulkanCleanup(Terrain* terrain, U32 frames_in_flight);
internal void
UpdateTerrainUniformBuffer(Terrain* terrain, glm::mat4* view, glm::mat4* proj, Vec2F32 screen_res,
                           U32 current_image);

internal void
TerrainDescriptorPoolCreate(Terrain* terrain, U32 frames_in_flight);
internal void
TerrainDescriptorSetCreate(Terrain* terrain, U32 frames_in_flight);

internal void
TerrainRenderPassBegin(VulkanContext* vk_ctx, Terrain* terrain, U32 image_index, U32 current_frame);

internal void
TerrainInit();

internal VkVertexInputBindingDescription
TerrainBindingDescriptionGet();

internal Buffer<VkVertexInputAttributeDescription>
TerrainAttributeDescriptionGet(Arena* arena);

internal void
TerrainTextureResourceCreate(VulkanContext* vk_ctx, Terrain* terrain, const char* cwd);

internal void
TerrainGenerateBuffers(Arena* arena, Buffer<Vertex>* vertices, Buffer<U32>* indices,
                       U32 patch_size);
