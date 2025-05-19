#pragma once

enum BoxAttributes
{
    HOT = (1 << 0),
    ACTIVE = (1 << 1)
};

struct Vulkan_BoxInstance
{
    Vec2<f32> pos0;
    Vec2<f32> pos1;
    F32Vec4 color;
    f32 softness;
    f32 borderThickness;
    f32 cornerRadius;
    u32 attributes;

    static VkVertexInputBindingDescription
    getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vulkan_BoxInstance);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }
    static VkVertexInputAttributeDescription_Buffer
    getAttributeDescriptions(Arena* arena)
    {
        VkVertexInputAttributeDescription_Buffer attributeDescriptions =
            VkVertexInputAttributeDescription_Buffer_Alloc(arena, 7);
        attributeDescriptions.data[0].binding = 0;
        attributeDescriptions.data[0].location = 0;
        attributeDescriptions.data[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions.data[0].offset = offsetof(Vulkan_BoxInstance, pos0);

        attributeDescriptions.data[1].binding = 0;
        attributeDescriptions.data[1].location = 1;
        attributeDescriptions.data[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions.data[1].offset = offsetof(Vulkan_BoxInstance, pos1);

        attributeDescriptions.data[2].binding = 0;
        attributeDescriptions.data[2].location = 2;
        attributeDescriptions.data[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions.data[2].offset = offsetof(Vulkan_BoxInstance, color);

        attributeDescriptions.data[3].binding = 0;
        attributeDescriptions.data[3].location = 3;
        attributeDescriptions.data[3].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions.data[3].offset = offsetof(Vulkan_BoxInstance, softness);

        attributeDescriptions.data[4].binding = 0;
        attributeDescriptions.data[4].location = 4;
        attributeDescriptions.data[4].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions.data[4].offset = offsetof(Vulkan_BoxInstance, borderThickness);

        attributeDescriptions.data[5].binding = 0;
        attributeDescriptions.data[5].location = 5;
        attributeDescriptions.data[5].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions.data[5].offset = offsetof(Vulkan_BoxInstance, cornerRadius);

        attributeDescriptions.data[6].binding = 0;
        attributeDescriptions.data[6].location = 6;
        attributeDescriptions.data[6].format = VK_FORMAT_R32_UINT;
        attributeDescriptions.data[6].offset = offsetof(Vulkan_BoxInstance, attributes);

        return attributeDescriptions;
    }
};

struct Box
{
    Box* next;
    Vec2<f32> pos0;
    Vec2<f32> pos1;
    F32Vec4 color;
    f32 softness;
    f32 borderThickness;
    f32 cornerRadius;
    u32 attributes;
};

struct BoxQueue
{
    Box* first;
    Box* last;
};

struct BoxContext
{
    Array<Vulkan_BoxInstance> boxInstances;
    BoxQueue boxQueue;
    u64 numInstances;
    static const u64 MAX_BOX_INSTANCES = 1000;

    // vulkan part
    VkBuffer instBuffer;
    VkDeviceMemory instMemoryBuffer;
    VkDeviceSize instBufferSize;
    VkBuffer indexBuffer;
    VkDeviceMemory indexMemoryBuffer;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
};

root_function void
BoxIndexBufferCreate(BoxContext* box_context, VkPhysicalDevice physicalDevice, VkDevice device,
                     VkCommandPool commandPool, VkQueue graphicsQueue, u16_Buffer indices);

root_function void
BoxRenderPassBegin(BoxContext* box_context, VulkanContext* vulkanContext, u32 imageIndex,
                   u32 currentFrame);

root_function void
BoxCleanup(BoxContext* box_context, VkDevice device);

root_function void
InstanceBufferFillFromBoxes(BoxContext* box_context, VkPhysicalDevice physicalDevice,
                            VkDevice device);

root_function u64
InstanceBufferFromBoxes(Box* first, Array<Vulkan_BoxInstance> outBuffer);

root_function void
BoxFrameReset(Arena* arena, BoxContext* box_context);
