#pragma once

// ~mgj: forward declaration
struct IO; // TODO: remove this after refactor or maybe not
namespace city
{
struct City;
struct RoadVertex;
} // namespace city
namespace wrapper
{
struct VulkanContext;
}
namespace ui
{
struct Camera;
}

namespace wrapper
{

namespace internal
{
struct BufferContext
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    U32 size;
    U32 capacity;
};

struct QueueFamilyIndices
{
    U32 graphicsFamilyIndex;
    U32 presentFamilyIndex;
};

struct ShaderModuleInfo
{
    VkPipelineShaderStageCreateInfo info;
    VkDevice device;

    ~ShaderModuleInfo()
    {
        vkDestroyShaderModule(device, info.module, nullptr);
    }
};

struct QueueFamilyIndexBits
{
    U32 graphicsFamilyIndexBits;
    U32 presentFamilyIndexBits;
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    Buffer<VkSurfaceFormatKHR> formats;
    Buffer<VkPresentModeKHR> presentModes;
};

enum PlaneType
{
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    BACK,
    FRONT,
    PlaneType_Count
};

struct Frustum
{
    glm::vec4 planes[PlaneType_Count];
};

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
};

static void
DescriptorPoolCreate(VulkanContext* vk_ctx);

static ShaderModuleInfo
ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);
template <typename T>
static void
VkBufferFromBuffers(VkDevice device, VkPhysicalDevice physical_device, BufferContext* vk_buffer_ctx,
                    Buffer<Buffer<T>> buffers, VkBufferUsageFlags usage);

template <typename T>
static void
VkBufferFromBufferMapping(VkDevice device, VkPhysicalDevice physical_device,
                          BufferContext* vk_buffer_ctx, Buffer<T> buffer, VkBufferUsageFlags usage);

static QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

static bool
VK_QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

static QueueFamilyIndexBits
VK_QueueFamiliesFind(VulkanContext* vk_ctx, VkPhysicalDevice device);

static void
VK_BufferContextCleanup(VkDevice device, BufferContext* buffer_context);

static bool
VK_IsDeviceSuitable(VulkanContext* vk_ctx, VkPhysicalDevice device, QueueFamilyIndexBits indexBits);

static SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VulkanContext* vk_ctx, VkPhysicalDevice device);

// ~mgj: Road
static VkVertexInputBindingDescription
RoadBindingDescriptionGet();
static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena);
static void
RoadPipelineCreate(city::City* city, String8 cwd);

//~mgj: camera
static void
CameraCleanup(VulkanContext* vk_ctx);

static void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix);
// ~mgj: camera functions
static void
CameraUniformBufferCreate(VulkanContext* vk_ctx);
static void
CameraUniformBufferUpdate(VulkanContext* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                          U32 current_frame);
static void
CameraDescriptorSetLayoutCreate(VulkanContext* vk_ctx);
static void
CameraDescriptorSetCreate(VulkanContext* vk_ctx);

} // namespace internal

struct Road
{
    internal::BufferContext vertex_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
};

struct VulkanContext
{
    Arena* arena;

    const U32 WIDTH = 800;
    const U32 HEIGHT = 600;
    static const U32 MAX_FRAMES_IN_FLIGHT = 2;

    const char* validation_layers[1] = {"VK_LAYER_KHRONOS_validation"};

    const char* device_extensions[2] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};

#ifdef NDEBUG
    const U8 enable_validation_layers = 0;
#else
    const U8 enable_validation_layers = 1;
#endif
    U8 framebuffer_resized = 0;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
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

    Buffer<VkSemaphore> image_available_semaphores;
    Buffer<VkSemaphore> render_finished_semaphores;
    Buffer<VkFence> in_flight_fences;
    U32 current_frame = 0;

    VkFormat color_attachment_format;
    VkImage color_image;
    VkDeviceMemory color_image_memory;
    VkImageView color_image_view;

    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkFormat depth_attachment_format;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    internal::BufferContext vk_vertex_context;
    internal::BufferContext vk_indice_context;

    // queue
    internal::QueueFamilyIndices queue_family_indices;

    VkDescriptorPool descriptor_pool;
    // ~mgj: camera resources for uniform buffers
    VkBuffer camera_buffer[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory camera_buffer_memory[MAX_FRAMES_IN_FLIGHT];
    void* camera_buffer_memory_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[MAX_FRAMES_IN_FLIGHT];
    internal::CameraUniformBuffer camera_uniform_buffer;
};

struct RoadPushConstants
{
    glm::mat4 model;
    F32 road_width;
    F32 road_height;
};

// ~mgj: road function
static void
RoadInit(wrapper::VulkanContext* vk_ctx, city::City* city, String8 cwd);
static void
RoadUpdate(city::Road* road, Road* w_road, wrapper::VulkanContext* vk_ctx, U32 image_index);
static void
RoadCleanup(city::City* city, wrapper::VulkanContext* vk_ctx);

struct Vulkan_PushConstantInfo
{
    uint32_t offset;
    uint32_t size;
};

struct SwapChainInfo
{
    internal::SwapChainSupportDetails swapChainSupport;
    VkSurfaceFormatKHR surfaceFormat;
    VkPresentModeKHR presentMode;
    VkExtent2D extent;
};

// buffers helpers
static void
VK_BufferCreate(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer,
                VkDeviceMemory* bufferMemory);

// image helpers

static void
VK_ImageFromBufferCopy(VkCommandBuffer command_buffer, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height);

static void
VK_ImageCreate(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height,
               VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
               VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image,
               VkDeviceMemory* imageMemory, U32 mipmap_level);

static void
VK_ImageViewCreate(VkImageView* out_image_view, VkDevice device, VkImage image, VkFormat format,
                   VkImageAspectFlags aspect_mask, U32 mipmap_level);

static void
VK_ImageLayoutTransition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
                         VkImageLayout oldLayout, VkImageLayout newLayout, U32 mipmap_level);

static void
VK_GenerateMipmaps(VkCommandBuffer command_buffer, VkImage image, int32_t tex_width,
                   int32_t text_height, uint32_t mip_levels);
// sampler helpers
static void
VK_SamplerCreate(VkSampler* sampler, VkDevice device, VkFilter filter,
                 VkSamplerMipmapMode mipmap_mode, U32 mip_level_count, F32 max_anisotrophy);

// queue family

static void
VK_DepthResourcesCreate(VulkanContext* vk_context);

static void
VK_RecreateSwapChain(IO* io_ctx, VulkanContext* vk_ctx);

static void
VK_SyncObjectsCreate(VulkanContext* vk_ctx);

static void
VK_SwapChainImageViewsCreate(VulkanContext* vk_ctx);
static void
VK_CommandPoolCreate(VulkanContext* vk_ctx);

static void
VK_ColorResourcesCleanup(VulkanContext* vk_ctx);
static void
VK_SwapChainCleanup(VulkanContext* vk_ctx);
static void
VK_CreateInstance(VulkanContext* vk_ctx);
static void
VK_DebugMessengerSetup(VulkanContext* vk_ctx);
static void
VK_SurfaceCreate(VulkanContext* vk_ctx, IO* io_ctx);
static void
VK_PhysicalDevicePick(VulkanContext* vk_ctx);
static void
VK_LogicalDeviceCreate(Arena* arena, VulkanContext* vk_ctx);

static SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vk_ctx, IO* io_ctx);
static U32
VK_SwapChainImageCountGet(VulkanContext* vk_ctx);
static void
VK_SwapChainImagesCreate(VulkanContext* vk_ctx, SwapChainInfo swapChainInfo, U32 imageCount);

static VkExtent2D
VK_ChooseSwapExtent(IO* io_ctx, VulkanContext* vk_ctx,
                    const VkSurfaceCapabilitiesKHR& capabilities);

static Buffer<String8>
VK_RequiredExtensionsGet(VulkanContext* vk_ctx);

static void
VK_PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

static VkSampleCountFlagBits
VK_MaxUsableSampleCountGet(VkPhysicalDevice device);

static bool
VK_CheckDeviceExtensionSupport(VulkanContext* vk_ctx, VkPhysicalDevice device);

static bool
VK_CheckValidationLayerSupport(VulkanContext* vk_ctx);

static VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats);

static VkPresentModeKHR
VK_ChooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes);

static void
VK_ColorResourcesCreate(VkPhysicalDevice physicalDevice, VkDevice device,
                        VkFormat swapChainImageFormat, VkExtent2D swapChainExtent,
                        VkSampleCountFlagBits msaaSamples, VkImageView* out_color_image_view,
                        VkImage* out_color_image, VkDeviceMemory* out_color_image_memory);

static void
VK_CommandBuffersCreate(VulkanContext* vk_ctx);

static VkResult
CreateDebugUtilsMessengerEXT(VkInstance instance,
                             const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDebugUtilsMessengerEXT* pDebugMessenger);

static void
DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                              const VkAllocationCallbacks* pAllocator);

static VkCommandBuffer
VK_BeginSingleTimeCommands(VulkanContext* vk_ctx);

static void
VK_EndSingleTimeCommands(VulkanContext* vk_ctx, VkCommandBuffer commandBuffer);
static void
VK_VulkanInit(VulkanContext* vk_ctx, IO* io_ctx);
static void
VK_Cleanup();

} // namespace wrapper
