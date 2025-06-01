#pragma once

struct Vulkan_PushConstantInfo
{
    uint32_t offset;
    uint32_t size;
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    Buffer<VkSurfaceFormatKHR> formats;
    Buffer<VkPresentModeKHR> presentModes;
};

// queue family
struct QueueFamilyIndexBits
{
    U32 graphicsFamilyIndexBits;
    U32 presentFamilyIndexBits;
};

struct QueueFamilyIndices
{
    U32 graphicsFamilyIndex;
    U32 presentFamilyIndex;
};

struct SwapChainInfo
{
    SwapChainSupportDetails swapChainSupport;
    VkSurfaceFormatKHR surfaceFormat;
    VkPresentModeKHR presentMode;
    VkExtent2D extent;
};

struct Vertex
{
    Vec3F32 pos;
};

struct VK_BufferContext
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    U32 size;
    U32 capacity;
};

// vulkan context
struct VulkanContext
{
    Arena* arena;

    const U32 WIDTH = 800;
    const U32 HEIGHT = 600;
    const U32 MAX_FRAMES_IN_FLIGHT = 2;

    const char* validation_layers[1] = {"VK_LAYER_KHRONOS_validation"};

    const char* device_extensions[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const u8 enableValidationLayers = 0;
#else
    const U8 enable_validation_layers = 1;
#endif
    U8 framebuffer_resized = 0;

    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkQueue graphics_queue;
    VkSurfaceKHR surface;
    VkQueue present_queue;
    VkSwapchainKHR swapchain;
    Buffer<VkImage> swapchain_images;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    Buffer<VkImageView> swapchain_image_views;

    Buffer<VkFramebuffer> swapchain_framebuffers;
    VkCommandPool command_pool;
    Buffer<VkCommandBuffer> command_buffers;
    VkRenderPass vk_renderpass;

    Buffer<VkSemaphore> image_available_semaphores;
    Buffer<VkSemaphore> render_finished_semaphores;
    Buffer<VkFence> in_flight_fences;
    U32 current_frame = 0;

    VkImage color_image;
    VkDeviceMemory color_image_memory;
    VkImageView color_image_view;
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    VK_BufferContext vk_vertex_context;
    VK_BufferContext vk_indice_context;

    // queue
    QueueFamilyIndices queue_family_indices;
};

internal VkCommandBuffer
beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);

internal void
endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                      VkCommandBuffer commandBuffer);

internal void
BufferCreate(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer,
             VkDeviceMemory* bufferMemory);
internal void
copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
           VkBuffer dstBuffer, VkDeviceSize size);

internal void
transitionImageLayout(VkCommandPool commandPool, VkDevice device, VkQueue graphicsQueue,
                      VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

internal void
copyBufferToImage(VkCommandPool commandPool, VkDevice device, VkQueue queue, VkBuffer buffer,
                  VkImage image, uint32_t width, uint32_t height);
internal void
createImage(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height,
            VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
            VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
            VkDeviceMemory& imageMemory);

internal VkImageView
createImageView(VkDevice device, VkImage image, VkFormat format);

internal VkImageView
createColorResources(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkFormat swapChainImageFormat, VkExtent2D swapChainExtent,
                     VkSampleCountFlagBits msaaSamples, VkImage& colorImage,
                     VkDeviceMemory& colorImageMemory);

internal void
createFramebuffers(VulkanContext* vulkan_ctx, VkRenderPass renderPass);

internal void
createGraphicsPipeline(VkPipelineLayout* pipelineLayout, VkPipeline* graphicsPipeline,
                       VkDevice device, VkExtent2D swapChainExtent, VkRenderPass renderPass,
                       VkDescriptorSetLayout descriptorSetLayout, VkSampleCountFlagBits msaaSamples,
                       VkVertexInputBindingDescription bindingDescription,
                       Buffer<VkVertexInputAttributeDescription> attributeDescriptions,
                       Vulkan_PushConstantInfo pushConstInfo, String8 vertShaderPath,
                       String8 fragShaderPath, VkShaderStageFlagBits pushConstantStage);

internal VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);

// queue family

internal bool
QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

internal QueueFamilyIndices
QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

internal QueueFamilyIndexBits
QueueFamiliesFind(VulkanContext* vulkanContext, VkPhysicalDevice device);

internal SwapChainSupportDetails
querySwapChainSupport(Arena* arena, VulkanContext* vulkanContext, VkPhysicalDevice device);

internal void
RenderPassCreate();

template <typename T>
internal void
VK_BufferContextCreate(VulkanContext* vk_ctx, VK_BufferContext* vk_buffer_ctx,
                       Buffer<Buffer<T>> buffers, VkBufferUsageFlags usage);