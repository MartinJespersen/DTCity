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

// ~mgj: Internals
namespace internal
{

struct BufferAllocation
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
};

struct BufferAllocationMapped
{
    BufferAllocation buffer_alloc;
    void* mapped_ptr;
    VkMemoryPropertyFlags mem_prop_flags;

    BufferAllocation staging_buffer_alloc;
    Arena* arena;
};

struct ImageAllocation
{
    VkImage image;
    VmaAllocation allocation;
    VkDeviceSize size;
};

struct ImageViewResource
{
    VkImageView image_view;
    VkDevice device;
};

struct ImageResource
{
    ImageAllocation image_alloc;
    ImageViewResource image_view_resource;
};

struct ImageSwapchainResource
{
    VkImage image;
    VkDeviceSize size;
    ImageViewResource image_view_resource;
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    Buffer<VkSurfaceFormatKHR> formats;
    Buffer<VkPresentModeKHR> presentModes;
};

struct SwapChainInfo
{
    SwapChainSupportDetails swapChainSupport;
    VkSurfaceFormatKHR surfaceFormat;
    VkPresentModeKHR presentMode;
    VkExtent2D extent;
};

struct BufferContext
{
    BufferAllocation buffer_alloc;
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
// ~mgj: Buffers helpers
//
// buffer usage patterns with VMA:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static internal::BufferAllocation
BufferAllocationCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                       VmaMemoryUsage vma_usage, VmaAllocationCreateFlags vma_flags);
static void
BufferMappedUpdate(VkCommandBuffer cmd_buffer, VmaAllocator allocator,
                   internal::BufferAllocationMapped mapped_buffer);

static internal::BufferAllocationMapped
BufferMappedCreate(VkCommandBuffer cmd_buffer, VmaAllocator allocator, VkDeviceSize size,
                   VkBufferUsageFlags buffer_usage);
static void
BufferDestroy(VmaAllocator allocator, internal::BufferAllocation* buffer_allocation);

static void
BufferMappedDestroy(VmaAllocator allocator, internal::BufferAllocationMapped* mapped_buffer);
// ~mgj: Images
//

static internal::ImageAllocation
ImageAllocationCreate(VmaAllocator allocator, U32 width, U32 height,
                      VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
                      VkImageUsageFlags usage, U32 mipmap_level, VmaMemoryUsage memory_usage,
                      VmaAllocationCreateFlags memory_properties);

static void
SwapChainImageResourceCreate(VulkanContext* vk_ctx, SwapChainInfo swapchain_info, U32 image_count);
static ImageViewResource
ImageViewResourceCreate(VkDevice device, VkImage image, VkFormat format,
                        VkImageAspectFlags aspect_mask, U32 mipmap_level);

static void
ImageViewResourceDestroy(internal::ImageViewResource image_view_resource);
static void
ImageAllocationDestroy(VmaAllocator allocator, ImageAllocation image_alloc);
static ImageResource
ImageResourceCreate(ImageViewResource image_view_resource, ImageAllocation image_alloc,
                    VkImageView image_view);
static void
ImageResourceDestroy(VmaAllocator allocator, internal::ImageResource image);

// ~mgj: Swapchain functions
static U32
VK_SwapChainImageCountGet(VulkanContext* vk_ctx);
static SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vk_ctx, IO* io_ctx);

static void
DescriptorPoolCreate(VulkanContext* vk_ctx);

static ShaderModuleInfo
ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);
template <typename T>
static void
VkBufferFromBuffers(VulkanContext* vk_ctx, BufferContext* vk_buffer_ctx, Buffer<Buffer<T>> buffers,
                    VkBufferUsageFlags usage);

template <typename T>
static void
VkBufferFromBufferMapping(VulkanContext* vk_ctx, BufferContext* vk_buffer_ctx, Buffer<T> buffer,
                          VkBufferUsageFlags usage);

static QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

static bool
VK_QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

static QueueFamilyIndexBits
VK_QueueFamiliesFind(VulkanContext* vk_ctx, VkPhysicalDevice device);

static void
BufferContextDestroy(VmaAllocator allocator, BufferContext* buffer_context);

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
static void
VK_ColorResourcesCreate(VulkanContext* vk_ctx);

} // namespace internal

struct Road
{
    internal::BufferContext vertex_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
};

struct VulkanContext
{
    static const U32 WIDTH = 800;
    static const U32 HEIGHT = 600;
    static const U32 MAX_FRAMES_IN_FLIGHT = 2;
#ifdef NDEBUG
    static const U8 enable_validation_layers = 0;
#else
    static const U8 enable_validation_layers = 1;
#endif
    Arena* arena;

    Buffer<String8> validation_layers;
    Buffer<String8> device_extensions;

    U8 framebuffer_resized;

    VmaAllocator allocator;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkQueue graphics_queue;
    VkSurfaceKHR surface;
    VkQueue present_queue;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    Buffer<internal::ImageSwapchainResource> swapchain_image_resources;

    VkCommandPool command_pool;
    Buffer<VkCommandBuffer> command_buffers;

    Buffer<VkSemaphore> image_available_semaphores;
    Buffer<VkSemaphore> render_finished_semaphores;
    Buffer<VkFence> in_flight_fences;
    U32 current_frame = 0;

    internal::ImageResource color_image_resource;
    VkFormat color_attachment_format;

    internal::ImageResource depth_image_resource;
    VkFormat depth_attachment_format;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    internal::BufferContext vk_vertex_context;
    internal::BufferContext vk_indice_context;

    // queue
    internal::QueueFamilyIndices queue_family_indices;

    VkDescriptorPool descriptor_pool;
    // ~mgj: camera resources for uniform buffers
    internal::BufferAllocationMapped camera_buffer_alloc_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[MAX_FRAMES_IN_FLIGHT];
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

// image helpers

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
static VulkanContext*
VK_VulkanInit(Arena* arena, IO* io_ctx);
static void
VK_Cleanup(VulkanContext* vk_ctx);

} // namespace wrapper
