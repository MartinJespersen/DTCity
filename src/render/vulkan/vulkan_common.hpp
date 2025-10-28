#pragma once

struct VK_Context;

struct VK_BufferAllocation
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
};

struct VK_BufferAllocationMapped
{
    VK_BufferAllocation buffer_alloc;
    void* mapped_ptr;
    VkMemoryPropertyFlags mem_prop_flags;

    VK_BufferAllocation staging_buffer_alloc;
    Arena* arena;
};

struct VK_BufferReadback
{
    VK_BufferAllocation buffer_alloc;
    void* mapped_ptr;
};

struct VK_ImageAllocation
{
    VkImage image;
    VmaAllocation allocation;
    VkDeviceSize size;
    VkExtent3D extent;
};

struct VK_ImageViewResource
{
    VkImageView image_view;
    VkDevice device;
};

struct VK_ImageResource
{
    VK_ImageAllocation image_alloc;
    VK_ImageViewResource image_view_resource;
};

struct VK_ImageSwapchainResource
{
    VkImage image;
    VkDeviceSize size;
    VK_ImageViewResource image_view_resource;
};

struct VK_SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    Buffer<VkSurfaceFormatKHR> formats;
    Buffer<VkPresentModeKHR> presentModes;
};

struct VK_SwapchainResources
{
    Arena* arena;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    VK_SwapChainSupportDetails swapchain_support;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    U32 image_count;

    VK_ImageResource color_image_resource;
    VkFormat color_format;
    VK_ImageResource depth_image_resource;
    VkFormat depth_format;
    Buffer<VK_ImageSwapchainResource> image_resources;

    // object id location resource
    VkFormat object_id_image_format;
    Buffer<VK_ImageResource> object_id_image_resources;
    Buffer<VK_ImageResource> object_id_image_resolve_resources;
    VK_BufferReadback object_id_buffer_readback;
};

struct VK_QueueFamilyIndices
{
    U32 graphicsFamilyIndex;
    U32 presentFamilyIndex;
};

struct VK_ShaderModuleInfo
{
    VkPipelineShaderStageCreateInfo info;
    VkDevice device;

    ~VK_ShaderModuleInfo()
    {
        vkDestroyShaderModule(device, info.module, nullptr);
    }
};

struct VK_QueueFamilyIndexBits
{
    U32 graphicsFamilyIndexBits;
    U32 presentFamilyIndexBits;
};

enum VK_PlaneType
{
    VK_PlaneType_Left,
    VK_PlaneType_Right,
    VK_PlaneType_Top,
    VK_PlaneType_Btm,
    VK_PlaneType_Back,
    VK_PlaneType_Front,
    VK_PlaneType_Count
};

struct VK_Frustum
{
    glm::vec4 planes[VK_PlaneType_Count];
};

// ~mgj: Buffers helpers
//
// buffer usage patterns with VMA:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static VK_BufferAllocation
VK_BufferAllocationCreate(VmaAllocator allocator, VkDeviceSize size,
                          VkBufferUsageFlags buffer_usage, VmaAllocationCreateInfo vma_info);
static void
VK_BufferMappedUpdate(VkCommandBuffer cmd_buffer, VmaAllocator allocator,
                      VK_BufferAllocationMapped mapped_buffer);

static VK_BufferAllocationMapped
VK_BufferMappedCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage);
static void
VK_BufferDestroy(VmaAllocator allocator, VK_BufferAllocation* buffer_allocation);
static void
VK_BufferMappedDestroy(VmaAllocator allocator, VK_BufferAllocationMapped* mapped_buffer);

static void
VK_BufferReadbackCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                        VK_BufferReadback* out_buffer_readback);
static void
VK_BufferReadbackDestroy(VmaAllocator allocator, VK_BufferReadback* out_buffer_readback);
static VK_BufferAllocation
VK_StagingBufferCreate(VmaAllocator allocator, VkDeviceSize size);

// ~mgj: Images
static VK_ImageAllocation
VK_ImageAllocationCreate(VmaAllocator allocator, U32 width, U32 height,
                         VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
                         VkImageUsageFlags usage, U32 mipmap_level,
                         VmaAllocationCreateInfo vma_info);
static void
VK_SwapChainImageResourceCreate(VkDevice device, VK_SwapchainResources* swapchain_resources,
                                U32 image_count);
static VK_ImageViewResource
VK_ImageViewResourceCreate(VkDevice device, VkImage image, VkFormat format,
                           VkImageAspectFlags aspect_mask, U32 mipmap_level);
static void
VK_ImageViewResourceDestroy(VK_ImageViewResource image_view_resource);
static void
VK_ImageAllocationDestroy(VmaAllocator allocator, VK_ImageAllocation image_alloc);

static void
VK_ImageResourceDestroy(VmaAllocator allocator, VK_ImageResource image);

// ~mgj: Swapchain functions
static U32
VK_SwapChainImageCountGet(VkDevice device, VK_SwapchainResources* swapchain_resources);
static VK_SwapchainResources*
VK_SwapChainCreate(VK_Context* vk_ctx, Vec2U32 framebuffer_dim);

static VK_ShaderModuleInfo
VK_ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
VK_ShaderModuleCreate(VkDevice device, Buffer<U8> buffer);

static void
VK_BufferAllocCreateOrResize(VmaAllocator allocator, U32 total_buffer_byte_count,
                             VK_BufferAllocation* buffer_alloc, VkBufferUsageFlags usage);

static VK_QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(VK_QueueFamilyIndexBits queueFamilyBits);

static bool
VK_QueueFamilyIsComplete(VK_QueueFamilyIndexBits queueFamily);

static VK_QueueFamilyIndexBits
VK_QueueFamiliesFind(VK_Context* vk_ctx, VkPhysicalDevice device);

static bool
VK_IsDeviceSuitable(VK_Context* vk_ctx, VkPhysicalDevice device, VK_QueueFamilyIndexBits indexBits);

static VK_SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VkPhysicalDevice device, VkSurfaceKHR surface);

static void
VK_FrustumPlanesCalculate(VK_Frustum* out_frustum, const glm::mat4 matrix);

static void
VK_ColorResourcesCreate(VK_Context* vk_ctx, VK_SwapchainResources* swapchain_resources);

// sampler helpers
static VkSampler
VK_SamplerCreate(VkDevice device, VkSamplerCreateInfo* sampler_info);

// ~mgj: Descriptor Related Functions
static void
VK_DescriptorPoolCreate(VK_Context* vk_ctx);
static VkDescriptorSet
VK_DescriptorSetCreate(Arena* arena, VkDevice device, VkDescriptorPool desc_pool,
                       VkDescriptorSetLayout desc_set_layout, VkImageView image_view,
                       VkSampler sampler);

static VkDescriptorSetLayout
VK_DescriptorSetLayoutCreate(VkDevice device, VkDescriptorSetLayoutBinding* bindings,
                             U32 binding_count);

// image helpers

// queue family

static void
VK_DepthResourcesCreate(VK_Context* vk_context, VK_SwapchainResources* swapchain_resources);

static void
VK_RecreateSwapChain(Vec2U32 framebuffer_dim, VK_Context* vk_ctx);

static void
VK_SyncObjectsCreate(VK_Context* vk_ctx);

static void
VK_SyncObjectsDestroy(VK_Context* vk_ctx);

static VkCommandPool
VK_CommandPoolCreate(VkDevice device, VkCommandPoolCreateInfo* poolInfo);

static void
VK_ColorResourcesCleanup(VK_Context* vk_ctx);
static void
VK_SwapChainCleanup(VkDevice device, VmaAllocator allocator,
                    VK_SwapchainResources* swapchain_resources);
static void
VK_CreateInstance(VK_Context* vk_ctx);
static void
VK_DebugMessengerSetup(VK_Context* vk_ctx);
static void
VK_SurfaceCreate(VK_Context* vk_ctx, io_IO* io_ctx);
static void
VK_PhysicalDevicePick(VK_Context* vk_ctx);
static void
VK_LogicalDeviceCreate(Arena* arena, VK_Context* vk_ctx);

static VkExtent2D
VK_ChooseSwapExtent(Vec2U32 framebuffer_dim, const VkSurfaceCapabilitiesKHR& capabilities);

static Buffer<String8>
VK_RequiredExtensionsGet(VK_Context* vk_ctx);

static void
VK_PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

static VkSampleCountFlagBits
VK_MaxUsableSampleCountGet(VkPhysicalDevice device);

static bool
VK_CheckDeviceExtensionSupport(VK_Context* vk_ctx, VkPhysicalDevice device);

static bool
VK_CheckValidationLayerSupport(VK_Context* vk_ctx);

static VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats);

static VkPresentModeKHR
VK_ChooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes);

static void
VK_CommandBuffersCreate(VK_Context* vk_ctx);

static VkResult
VK_CreateDebugUtilsMessengerEXT(VkInstance instance,
                                const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                VkDebugUtilsMessengerEXT* pDebugMessenger);

static void
VK_DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                 const VkAllocationCallbacks* pAllocator);

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
VK_SamplerCreateInfoFromSamplerInfo(R_SamplerInfo* sampler, VkSamplerCreateInfo* out_sampler_info);
