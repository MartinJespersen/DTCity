#pragma once

struct Character
{
    float width; // Size of glyph
    float height;
    float bearingX; // Offset from baseline to left/top of glyph
    float bearingY;
    unsigned int advance; // Offset to advance to next glyph
    u32 glyphOffset;
    char character;
};

struct Text
{
    String8 text;
    float x;
    float y;
};

struct Vulkan_GlyphInstance
{
    Vec2<f32> pos0;
    Vec2<f32> pos1;
    Vec2<f32> glyphOffset;

    static VkVertexInputBindingDescription
    getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vulkan_GlyphInstance);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }
    static VkVertexInputAttributeDescription_Buffer
    getAttributeDescriptions(Arena* arena)
    {
        VkVertexInputAttributeDescription_Buffer attributeDescriptions =
            VkVertexInputAttributeDescription_Buffer_Alloc(arena, 3);
        attributeDescriptions.data[0].binding = 0;
        attributeDescriptions.data[0].location = 0;
        attributeDescriptions.data[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions.data[0].offset = offsetof(Vulkan_GlyphInstance, pos0);

        attributeDescriptions.data[1].binding = 0;
        attributeDescriptions.data[1].location = 1;
        attributeDescriptions.data[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions.data[1].offset = offsetof(Vulkan_GlyphInstance, pos1);

        attributeDescriptions.data[2].binding = 0;
        attributeDescriptions.data[2].location = 2;
        attributeDescriptions.data[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions.data[2].offset = offsetof(Vulkan_GlyphInstance, glyphOffset);

        return attributeDescriptions;
    }
};

struct GlyphInstance
{
    Vec2<f32> pos0;
    Vec2<f32> pos1;
    Vec2<f32> glyphOffset;
    GlyphInstance* next;
};

struct Font
{
    Font* next;
    Font* prev;
    u32 fontSize;

    u64 instanceOffset;
    u64 instanceCount;
    GlyphInstance* instances;
    Array<Character> characters;
    static const u32 MAX_GLYPHS = 126;

    // vulkan
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;
    Array<VkDescriptorSet> descriptorSets;
};

struct FontLL
{
    Font* first;
    Font* last;
};

struct VK_DescriptorPool
{
    VK_DescriptorPool* next;
    VkDescriptorPool pool;
};

struct GlyphAtlas
{
    // persistent arena
    Arena* fontArena;
    FontLL fontLL;
    Font* fontFreeList;
    u32 fontCount;
    bool loaded;

    Array<Vulkan_GlyphInstance> glyphInstanceBuffer;
    u64 numInstances;
    u16_Buffer indices;
    static const u64 MAX_GLYPH_INSTANCES = 1000;

    // Vulkan part
    VkBuffer glyphInstBuffer;
    VkDeviceMemory glyphMemoryBuffer;
    VkDeviceSize glyphInstBufferSize;
    VkBuffer glyphIndexBuffer;
    VkDeviceMemory glyphIndexMemoryBuffer;

    VK_DescriptorPool* descriptor_pool;
    static const u64 descriptor_pool_size_default = 1;

    VkDescriptorSetLayout descriptorSetLayout;

    VkPipeline graphicsPipeline;
    VkPipelineLayout pipelineLayout;
};

root_function void
GlyphAtlasRenderPass(GlyphAtlas* glyphAtlas, VulkanContext* vulkanContext, u32 imageIndex,
                     u32 currentFrame);

root_function Vec2<float>
TextDimensionsCalculate(Font* font, String8 text);

root_function void
TextDraw(Font* font, String8 text, Vec2<f32> pos0, Vec2<f32> pos1);

root_function u64
InstanceBufferFromFontBuffers(Array<Vulkan_GlyphInstance> outBuffer, FontLL fontLL);

root_function void
mapGlyphInstancesToBuffer(GlyphAtlas* glyphAtlas, VkPhysicalDevice physicalDevice, VkDevice device,
                          VkQueue queue);

root_function void
createGlyphIndexBuffer(GlyphAtlas* glyphAtlas, VkPhysicalDevice physicalDevice, VkDevice device);

root_function Font*
FontAlloc(Arena* arena, Font* freeList);

root_function Font*
FontInit(GlyphAtlas* glyphAtlas, u32 fontSize);

root_function unsigned char*
initGlyphs(Arena* arena, Font* font, u32* width, u32* height);

root_function void
cleanupFontResources(GlyphAtlas* glyphAtlas, VkDevice device);

root_function void
GlyphAtlasImageCreate(Font* font, VkPhysicalDevice physicalDevice, VkDevice device,
                      VkCommandPool commandPool, VkQueue graphicsQueue);

root_function void
GlyphAtlasImageViewCreate(Font* font, VkDevice device);

root_function void
createGlyphAtlasTextureSampler(Font* font, VkPhysicalDevice physicalDevice, VkDevice device);

root_function void
FontDescriptorSetsCreate(Font* font, GlyphAtlas* glyph_atlas,
                         VkDescriptorSetLayout descriptorSetLayout, VkDevice device,
                         const u32 MAX_FRAMES_IN_FLIGHT, Array<VkDescriptorSet> descriptorSets);

root_function void
FontDescriptorPoolCreate(VkDevice device, GlyphAtlas* glyph_atlas, u32 descriptor_set_count);

root_function void
FontDescriptorSetLayoutCreate(VkDevice device, VkDescriptorSetLayout& descriptorSetLayout);

root_function Font*
FontFindOrCreate(GlyphAtlas* glyphAtlas, u32 fontSize);

root_function void
FontRenderResourcesAlloc(VulkanContext* vulkanContext, GlyphAtlas* glyphAtlas, Font* font);
root_function void
FontFrameReset(Arena* arena, GlyphAtlas* glyphAtlas);
