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

// vulkan context
struct VulkanContext
{
    Arena* arena;

    const U32 WIDTH = 800;
    const U32 HEIGHT = 600;
    const U32 MAX_FRAMES_IN_FLIGHT = 2;

    const char* validationLayers[1] = {"VK_LAYER_KHRONOS_validation"};

    const char* deviceExtensions[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const u8 enableValidationLayers = 0;
#else
    const U8 enableValidationLayers = 1;
#endif
    U8 framebufferResized = 0;

    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    VkQueue presentQueue;
    VkSwapchainKHR swapChain;
    Buffer<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    Buffer<VkImageView> swapChainImageViews;

    Buffer<VkFramebuffer> swapChainFramebuffers;
    VkCommandPool commandPool;
    Buffer<VkCommandBuffer> commandBuffers;

    Buffer<VkSemaphore> imageAvailableSemaphores;
    Buffer<VkSemaphore> renderFinishedSemaphores;
    Buffer<VkFence> inFlightFences;
    U32 currentFrame = 0;

    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    Vulkan_PushConstantInfo resolutionInfo;
    Buffer<U16> indices;

    // queue
    QueueFamilyIndices queueFamilyIndices;
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
createFramebuffers(Buffer<VkFramebuffer> framebuffers, VkDevice device, VkImageView colorImageView,
                   VkRenderPass renderPass, VkExtent2D swapChainExtent,
                   Buffer<VkImageView> swapChainImageViews);

internal void
createGraphicsPipeline(VkPipelineLayout* pipelineLayout, VkPipeline* graphicsPipeline,
                       VkDevice device, VkExtent2D swapChainExtent, VkRenderPass renderPass,
                       VkDescriptorSetLayout descriptorSetLayout, VkSampleCountFlagBits msaaSamples,
                       VkVertexInputBindingDescription bindingDescription,
                       Buffer<VkVertexInputAttributeDescription> attributeDescriptions,
                       Vulkan_PushConstantInfo pushConstInfo, String8 vertShaderPath,
                       String8 fragShaderPath, VkShaderStageFlagBits pushConstantStage);

internal VkRenderPass
createRenderPass(VkDevice device, VkFormat swapChainImageFormat, VkSampleCountFlagBits msaaSamples,
                 VkAttachmentLoadOp loadOp, VkImageLayout initialLayout, VkImageLayout finalLayout);

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
