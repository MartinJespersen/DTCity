#pragma once

struct TerrainUniformBuffer
{
    float displacement_factor;
    float tessellation_factor;
    float patch_size;
    float tessellated_edge_size;
};

namespace terrain
{
struct Vertex
{
    Vec3F32 pos;
    Vec2F32 uv;
};
} // namespace terrain

struct Terrain
{
    TerrainUniformBuffer uniform_buffer;

    Buffer<terrain::Vertex> vertices;
    Buffer<U32> indices;
    U32 patch_size;

    // vulkan
    VkBuffer* buffer;
    VkDeviceMemory* buffer_memory;
    void** buffer_memory_mapped;

    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet* descriptor_sets;

    VkImage vk_texture_image;
    VkDeviceMemory vk_texture_image_memory;
    VkImageView vk_texture_image_view;
    VkSampler vk_texture_sampler;
    U32 vk_mip_levels;
    VkFormat vk_texture_blit_format;
};

static void
TerrainAllocations(Arena* arena, Terrain* terrain, U32 frames_in_flight);

static void
TerrainDescriptorSetLayoutCreate(VkDevice device, Terrain* terrain);

static void
TerrainUniformBufferCreate(Terrain* terrain, U32 frames_in_flight);

static void
TerrainGraphicsPipelineCreate(Terrain* terrain, const char* cwd);

static void
TerrainVulkanCleanup(Terrain* terrain, U32 frames_in_flight);
static void
UpdateTerrainUniformBuffer(Terrain* terrain, Vec2F32 screen_res, U32 current_image);

static void
TerrainDescriptorSetCreate(wrapper::VulkanContext* vk_ctx, Terrain* terrain, U32 frames_in_flight);

static void
TerrainRenderPassBegin(wrapper::VulkanContext* vk_ctx, Terrain* terrain, U32 image_index,
                       U32 current_frame);

static void
TerrainInit();

static VkVertexInputBindingDescription
TerrainBindingDescriptionGet();

static Buffer<VkVertexInputAttributeDescription>
TerrainAttributeDescriptionGet(Arena* arena);

static void
TerrainTextureResourceCreate(wrapper::VulkanContext* vk_ctx, Terrain* terrain, const char* cwd);

static void
TerrainGenerateBuffers(Arena* arena, Buffer<terrain::Vertex>* vertices, Buffer<U32>* indices,
                       U32 patch_size);
