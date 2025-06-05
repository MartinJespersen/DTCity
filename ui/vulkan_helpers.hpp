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
    Vec4F32 color;
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

    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkFormat depth_image_format;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    VK_BufferContext vk_vertex_context;
    VK_BufferContext vk_indice_context;

    // queue
    QueueFamilyIndices queue_family_indices;
};

internal void
VK_CommandBuffersCreate(VulkanContext* vk_ctx);

internal VkResult
CreateDebugUtilsMessengerEXT(VkInstance instance,
                             const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDebugUtilsMessengerEXT* pDebugMessenger);

internal void
DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                              const VkAllocationCallbacks* pAllocator);

internal VkCommandBuffer
VK_BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);

internal void
VK_EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                         VkCommandBuffer commandBuffer);

internal void
VK_BufferCreate(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer,
                VkDeviceMemory* bufferMemory);

internal void
VK_ImageCreate(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height,
               VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
               VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image,
               VkDeviceMemory* imageMemory);

internal void
VK_ImageViewCreate(VkImageView* out_image_view, VkDevice device, VkImage image, VkFormat format,
                   VkImageAspectFlags aspect_mask);

internal void
VK_ColorResourcesCreate(VkPhysicalDevice physicalDevice, VkDevice device,
                        VkFormat swapChainImageFormat, VkExtent2D swapChainExtent,
                        VkSampleCountFlagBits msaaSamples, VkImageView* out_color_image_view,
                        VkImage* out_color_image, VkDeviceMemory* out_color_image_memory);

internal void
VK_FramebuffersCreate(VulkanContext* vulkan_ctx, VkRenderPass renderPass);

internal VkShaderModule
VK_ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);

// queue family

internal bool
VK_QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

internal QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

internal QueueFamilyIndexBits
VK_QueueFamiliesFind(VulkanContext* vulkanContext, VkPhysicalDevice device);

internal SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VulkanContext* vulkanContext, VkPhysicalDevice device);

internal void
VK_RenderPassCreate();

template <typename T>
internal void
VK_BufferContextCreate(VulkanContext* vk_ctx, VK_BufferContext* vk_buffer_ctx,
                       Buffer<Buffer<T>> buffers, VkBufferUsageFlags usage);

internal void
VK_DepthResourcesCreate(VulkanContext* vk_context);

internal void
VK_RecreateSwapChain(VulkanContext* vulkanContext);

internal void
VK_SyncObjectsCreate(VulkanContext* vulkanContext);

internal void
VK_SwapChainImageViewsCreate(VulkanContext* vulkanContext);
internal void
VK_CommandPoolCreate(VulkanContext* vulkanContext);

internal void
VK_ColorResourcesCleanup(VulkanContext* vulkanContext);
internal void
VK_SwapChainCleanup(VulkanContext* vulkanContext);
internal void
VK_CreateInstance(VulkanContext* vulkanContext);
internal void
VK_DebugMessengerSetup(VulkanContext* vulkanContext);
internal void
VK_SurfaceCreate(VulkanContext* vulkanContext);
internal void
VK_PhysicalDevicePick(VulkanContext* vulkanContext);
internal void
VK_LogicalDeviceCreate(Arena* arena, VulkanContext* vulkanContext);

internal SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vulkanContext);
internal U32
VK_SwapChainImageCountGet(VulkanContext* vulkanContext);
internal void
VK_SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount);

internal VkExtent2D
VK_ChooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities);

internal Buffer<String8>
VK_RequiredExtensionsGet(VulkanContext* vulkanContext);

internal void
VK_PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

internal bool
VK_IsDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                    QueueFamilyIndexBits indexBits);

internal VkSampleCountFlagBits
VK_MaxUsableSampleCountGet(VkPhysicalDevice device);

internal bool
VK_CheckDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device);

internal bool
VK_CheckValidationLayerSupport(VulkanContext* vulkanContext);

internal VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats);

internal VkPresentModeKHR
VK_ChooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes);
