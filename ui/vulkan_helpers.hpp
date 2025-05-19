#pragma once

BufferDec(VkSurfaceFormatKHR);
BufferDec(VkPresentModeKHR);
BufferDec(VkImage);
BufferDec(VkImageView);
BufferDec(VkFramebuffer);
BufferDec(VkCommandBuffer);
BufferDec(VkSemaphore);
BufferDec(VkFence);
BufferDec(VkVertexInputAttributeDescription);
BufferDec(VkDescriptorSetLayout);

struct Vulkan_PushConstantInfo
{
    uint32_t offset;
    uint32_t size;
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR_Buffer formats;
    VkPresentModeKHR_Buffer presentModes;
};

// queue family
struct QueueFamilyIndexBits
{
    u32 graphicsFamilyIndexBits;
    u32 presentFamilyIndexBits;
};

struct QueueFamilyIndices
{
    u32 graphicsFamilyIndex;
    u32 presentFamilyIndex;
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

    const u32 WIDTH = 800;
    const u32 HEIGHT = 600;
    const u32 MAX_FRAMES_IN_FLIGHT = 2;

    const char* validationLayers[1] = {"VK_LAYER_KHRONOS_validation"};

    const char* deviceExtensions[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const u8 enableValidationLayers = 0;
#else
    const u8 enableValidationLayers = 1;
#endif
    u8 framebufferResized = 0;

    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    VkQueue presentQueue;
    VkSwapchainKHR swapChain;
    VkImage_Buffer swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkImageView_Buffer swapChainImageViews;

    VkFramebuffer_Buffer swapChainFramebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer_Buffer commandBuffers;

    VkSemaphore_Buffer imageAvailableSemaphores;
    VkSemaphore_Buffer renderFinishedSemaphores;
    VkFence_Buffer inFlightFences;
    u32 currentFrame = 0;

    // TODO: add descriptor set layout and descriptor set to glyph atlas and rectangle

    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderPass boxRenderPass;
    VkRenderPass fontRenderPass;

    Vulkan_PushConstantInfo resolutionInfo;
    u16_Buffer indices;

    // queue
    QueueFamilyIndices queueFamilyIndices;
};

root_function VkCommandBuffer
beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);

root_function void
endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                      VkCommandBuffer commandBuffer);

root_function void
BufferCreate(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
             VkDeviceMemory& bufferMemory);
root_function void
copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
           VkBuffer dstBuffer, VkDeviceSize size);

root_function void
transitionImageLayout(VkCommandPool commandPool, VkDevice device, VkQueue graphicsQueue,
                      VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

root_function void
copyBufferToImage(VkCommandPool commandPool, VkDevice device, VkQueue queue, VkBuffer buffer,
                  VkImage image, uint32_t width, uint32_t height);
root_function void
createImage(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height,
            VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
            VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
            VkDeviceMemory& imageMemory);

root_function VkImageView
createImageView(VkDevice device, VkImage image, VkFormat format);

root_function VkImageView
createColorResources(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkFormat swapChainImageFormat, VkExtent2D swapChainExtent,
                     VkSampleCountFlagBits msaaSamples, VkImage& colorImage,
                     VkDeviceMemory& colorImageMemory);

root_function void
createFramebuffers(VkFramebuffer_Buffer framebuffers, VkDevice device, VkImageView colorImageView,
                   VkRenderPass renderPass, VkExtent2D swapChainExtent,
                   VkImageView_Buffer swapChainImageViews);

root_function void
createGraphicsPipeline(VkPipelineLayout* pipelineLayout, VkPipeline* graphicsPipeline,
                       VkDevice device, VkExtent2D swapChainExtent, VkRenderPass renderPass,
                       VkDescriptorSetLayout descriptorSetLayout, VkSampleCountFlagBits msaaSamples,
                       VkVertexInputBindingDescription bindingDescription,
                       VkVertexInputAttributeDescription_Buffer attributeDescriptions,
                       Vulkan_PushConstantInfo pushConstInfo, String8 vertShaderPath,
                       String8 fragShaderPath, VkShaderStageFlagBits pushConstantStage);

root_function VkRenderPass
createRenderPass(VkDevice device, VkFormat swapChainImageFormat, VkSampleCountFlagBits msaaSamples,
                 VkAttachmentLoadOp loadOp, VkImageLayout initialLayout, VkImageLayout finalLayout);

root_function VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer buffer);

// queue family

root_function bool
QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

root_function QueueFamilyIndices
QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

root_function QueueFamilyIndexBits
QueueFamiliesFind(VulkanContext* vulkanContext, VkPhysicalDevice device);

root_function SwapChainSupportDetails
querySwapChainSupport(Arena* arena, VulkanContext* vulkanContext, VkPhysicalDevice device);
