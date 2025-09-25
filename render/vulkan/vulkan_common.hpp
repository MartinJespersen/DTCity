#pragma once

// ~mgj: forward declaration
struct IO; // TODO: remove this after refactor or maybe not

namespace wrapper
{
struct VulkanContext;
} // namespace wrapper

namespace wrapper
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

struct BufferReadback
{
    BufferAllocation buffer_alloc;
    void* mapped_ptr;
};

struct ImageAllocation
{
    VkImage image;
    VmaAllocation allocation;
    VkDeviceSize size;
    VkExtent3D extent;
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

struct SwapchainResources
{
    Arena* arena;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    SwapChainSupportDetails swapchain_support;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;

    ImageResource color_image_resource;
    VkFormat color_format;
    ImageResource depth_image_resource;
    VkFormat depth_format;
    Buffer<ImageSwapchainResource> image_resources;

    // object id location resource
    VkFormat object_id_image_format;
    Buffer<ImageResource> object_id_image_resources;
    Buffer<ImageResource> object_id_image_resolve_resources;
    BufferReadback object_id_buffer_readback;
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

struct Texture
{
    BufferAllocation staging_buffer;
    ImageResource image_resource;
    VkSampler sampler;
    VkDescriptorSet desc_set;
    R_PipelineUsageType pipeline_usage_type;
};

// ~mgj: Buffers helpers
//
// buffer usage patterns with VMA:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static BufferAllocation
BufferAllocationCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                       VmaAllocationCreateInfo vma_info);
static void
BufferMappedUpdate(VkCommandBuffer cmd_buffer, VmaAllocator allocator,
                   BufferAllocationMapped mapped_buffer);

static BufferAllocationMapped
BufferMappedCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage);
static void
BufferDestroy(VmaAllocator allocator, BufferAllocation* buffer_allocation);
static void
BufferMappedDestroy(VmaAllocator allocator, BufferAllocationMapped* mapped_buffer);

static void
BufferReadbackCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                     BufferReadback* out_buffer_readback);
static void
BufferReadbackDestroy(VmaAllocator allocator, BufferReadback* out_buffer_readback);
static BufferAllocation
StagingBufferCreate(VmaAllocator allocator, VkDeviceSize size);

template <typename T>
static BufferAllocation
BufferUploadDevice(VkCommandBuffer cmd_buffer, BufferAllocation staging_buffer,
                   VulkanContext* vk_ctx, Buffer<T> buffer_host, VkBufferUsageFlagBits usage);
// ~mgj: Images
static ImageAllocation
ImageAllocationCreate(VmaAllocator allocator, U32 width, U32 height,
                      VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
                      VkImageUsageFlags usage, U32 mipmap_level, VmaAllocationCreateInfo vma_info);
static void
SwapChainImageResourceCreate(VkDevice device, SwapchainResources* swapchain_resources,
                             U32 image_count);
static ImageViewResource
ImageViewResourceCreate(VkDevice device, VkImage image, VkFormat format,
                        VkImageAspectFlags aspect_mask, U32 mipmap_level);
static void
ImageViewResourceDestroy(ImageViewResource image_view_resource);
static void
ImageAllocationDestroy(VmaAllocator allocator, ImageAllocation image_alloc);

static void
ImageResourceDestroy(VmaAllocator allocator, ImageResource image);

// ~mgj: Swapchain functions
static U32
VK_SwapChainImageCountGet(VkDevice device, SwapchainResources* swapchain_resources);
static SwapchainResources*
VK_SwapChainCreate(VulkanContext* vk_ctx, IO* io_ctx);

static ShaderModuleInfo
ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);

static void
BufferAllocCreateOrResize(VmaAllocator allocator, U32 total_buffer_byte_count,
                          BufferAllocation* buffer_alloc, VkBufferUsageFlags usage);

static QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits);

static bool
VK_QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily);

static QueueFamilyIndexBits
VK_QueueFamiliesFind(VulkanContext* vk_ctx, VkPhysicalDevice device);

static bool
VK_IsDeviceSuitable(VulkanContext* vk_ctx, VkPhysicalDevice device, QueueFamilyIndexBits indexBits);

static SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VkPhysicalDevice device, VkSurfaceKHR surface);

// ~mgj: Road
static VkVertexInputBindingDescription
RoadBindingDescriptionGet();
static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena);

static void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix);

static void
VK_ColorResourcesCreate(VulkanContext* vk_ctx, SwapchainResources* swapchain_resources);

// sampler helpers
static VkSampler
SamplerCreate(VkDevice device, VkSamplerCreateInfo* sampler_info);

// ~mgj: Descriptor Related Functions
static void
DescriptorPoolCreate(VulkanContext* vk_ctx);
static VkDescriptorSet
VK_DescriptorSetCreate(Arena* arena, VkDevice device, VkDescriptorPool desc_pool,
                       VkDescriptorSetLayout desc_set_layout, VkImageView image_view,
                       VkSampler sampler);

static VkDescriptorSet
DescriptorSetCreate(Arena* arena, VkDevice device, VkDescriptorPool desc_pool,
                    VkDescriptorSetLayout desc_set_layout, Texture* texture);

static VkDescriptorSetLayout
DescriptorSetLayoutCreate(VkDevice device, VkDescriptorSetLayoutBinding* bindings,
                          U32 binding_count);

struct Vulkan_PushConstantInfo
{
    uint32_t offset;
    uint32_t size;
};

// image helpers

// queue family

static void
VK_DepthResourcesCreate(VulkanContext* vk_context, SwapchainResources* swapchain_resources);

static void
VK_RecreateSwapChain(IO* io_ctx, VulkanContext* vk_ctx);

static void
VK_SyncObjectsCreate(VulkanContext* vk_ctx);

static void
SyncObjectsDestroy(VulkanContext* vk_ctx);

static VkCommandPool
VK_CommandPoolCreate(VkDevice device, VkCommandPoolCreateInfo* poolInfo);

static void
VK_ColorResourcesCleanup(VulkanContext* vk_ctx);
static void
VK_SwapChainCleanup(VkDevice device, VmaAllocator allocator,
                    SwapchainResources* swapchain_resources);
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
VK_ChooseSwapExtent(IO* io_ctx, const VkSurfaceCapabilitiesKHR& capabilities);

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

//
// ~mgj: texture functions
static void
TextureDestroy(VulkanContext* vk_ctx, Texture* texture);

#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            ERROR_LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__);       \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    }

// ~mgj: sampler functions
static void
VkSamplerCreateInfoFromSamplerInfo(R_SamplerInfo* sampler, VkSamplerCreateInfo* out_sampler_info);

} // namespace wrapper
// ~mgj: Format Conversion
static VkFormat
R_VkFormatFromTex2DFormat(R_Tex2DFormat format);
