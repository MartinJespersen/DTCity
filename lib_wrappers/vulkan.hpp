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

struct BufferAllocation
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
};

template <typename T> struct BufferInfo
{
    BufferAllocation buffer_alloc;
    Buffer<T> buffer;
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

struct PipelineInfo
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
};

struct CarInstance
{
    glm::vec4 row0;
    glm::vec4 row1;
    glm::vec4 row2;
    glm::vec4 row3;
};

struct CarVertex
{
    F32 position[3];
    F32 uv[2];
};

struct Texture
{
    ImageResource image_resource;
    VkSampler sampler;
    S32 width;
    S32 height;
    U32 mip_level_count;
};

struct Car
{
    PipelineInfo pipeline_info;
    BufferInfo<CarVertex> vertex_buffer_info;
    BufferInfo<U32> index_buffer_info;
    Buffer<CarInstance> car_instances; // each car has this.
    BufferContext instance_buffer_mapped;
    Texture* texture;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
};

static PipelineInfo
CarPipelineCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout layout);

static void
CarRendering(VulkanContext* vk_ctx, Car* car, U32 image_idx);
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
BufferDestroy(VmaAllocator allocator, BufferAllocation buffer_allocation);
static void
BufferMappedDestroy(VmaAllocator allocator, BufferAllocationMapped* mapped_buffer);

template <typename T>
static BufferAllocation
BufferUploadDevice(VkFence fence, VkCommandPool cmd_pool, VulkanContext* vk_ctx,
                   Buffer<T> buffer_host, VkBufferUsageFlagBits usage);
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
static ImageResource
ImageResourceCreate(ImageViewResource image_view_resource, ImageAllocation image_alloc,
                    VkImageView image_view);
static void
ImageResourceDestroy(VmaAllocator allocator, ImageResource image);
static void
ImageFromBufferCopy(VkCommandBuffer command_buffer, VkBuffer buffer, VkImage image, uint32_t width,
                    uint32_t height);

// ~mgj: Swapchain functions
static U32
VK_SwapChainImageCountGet(VkDevice device, SwapchainResources* swapchain_resources);
static SwapchainResources*
VK_SwapChainCreate(VulkanContext* vk_ctx, IO* io_ctx);

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
VkBufferFromBufferMapping(VmaAllocator allocator, BufferContext* vk_buffer_ctx, Buffer<T> buffer,
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
VK_QuerySwapChainSupport(Arena* arena, VkPhysicalDevice device, VkSurfaceKHR surface);

// ~mgj: Road
static VkVertexInputBindingDescription
RoadBindingDescriptionGet();
static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena);

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
VK_ColorResourcesCreate(VulkanContext* vk_ctx, SwapchainResources* swapchain_resources);

// sampler helpers
static VkSampler
SamplerCreate(VkDevice device, VkFilter filter, VkSamplerMipmapMode mipmap_mode,
              U32 mip_level_count, F32 max_anisotrophy);
// ~mgj: Descriptor Related Functions
static void
DescriptorPoolCreate(VulkanContext* vk_ctx);
static Buffer<VkDescriptorSet>
DescriptorSetCreate(Arena* arena, VkDevice device, VkDescriptorPool desc_pool,
                    VkDescriptorSetLayout desc_set_layout, Texture* texture, U32 frames_in_flight);

static VkDescriptorSetLayout
DescriptorSetLayoutCreate(VkDevice device, VkDescriptorSetLayoutBinding* bindings,
                          U32 binding_count);

struct Road
{
    Arena* arena;
    U64 texture_id;
    BufferContext vertex_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
    B32 descriptors_are_created;
    String8 road_texture_path;
};

struct AssetStoreTexture
{
    AssetStoreTexture* next;
    U64 id;
    B32 is_loaded;
    Texture asset;
};

struct TextureCreateInput
{
    VulkanContext* vk_ctx;
    Road* w_road;
};

struct RoadDescriptorCreateInfo
{
    VulkanContext* vk_ctx;
    city::Road* road;
    Road* w_road;
};

typedef void(TextureCreateFunc)(VkCommandPool cmd_pool, VkFence fence, TextureCreateInput* input,
                                Texture* texture);

struct RoadThreadInput
{
    AssetStoreTexture* asset_store_texture;

    TextureCreateFunc* texture_func;
    TextureCreateInput* texture_input;

    Buffer<VkCommandPool> cmd_pools;
    Buffer<VkFence> fences;
};

struct AssetStoreItemStateList
{
    AssetStoreTexture* first;
    AssetStoreTexture* last;
};

struct AssetStore
{
    Arena* arena;
    async::Queue* work_queue;
    Buffer<AssetStoreItemStateList> hashmap;
    Buffer<VkCommandPool> cmd_pools;
    Buffer<VkFence> fences;
    Buffer<VkDescriptorPool> descriptor_pools;
    AssetStoreTexture* free_list;
    U64 total_size;
    U64 used_size;
};

struct GraphicsQueue
{
    OS_Handle mutex;
    VkQueue graphics_queue;
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

    String8 texture_path;
    String8 shader_path;

    Buffer<String8> validation_layers;
    Buffer<String8> device_extensions;

    U8 framebuffer_resized;

    VmaAllocator allocator;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkFormat blit_format;
    // VkQueue graphics_queue;
    GraphicsQueue graphics_queue;
    VkSurfaceKHR surface;
    VkQueue present_queue;

    VkCommandPool command_pool;
    Buffer<VkCommandBuffer> command_buffers;

    Buffer<VkSemaphore> image_available_semaphores;
    Buffer<VkSemaphore> render_finished_semaphores;
    Buffer<VkFence> in_flight_fences;
    U32 current_frame = 0;

    SwapchainResources* swapchain_resources;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    BufferContext vk_vertex_context;
    BufferContext vk_indice_context;

    // queue
    QueueFamilyIndices queue_family_indices;

    VkDescriptorPool descriptor_pool;
    // ~mgj: camera resources for uniform buffers
    BufferAllocationMapped camera_buffer_alloc_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Car (move this!)
    Car* car;

    // ~mgj: Asset Streaming
    AssetStore* asset_store;
};

struct RoadPushConstants
{
    F32 road_height;
    F32 texture_scale;
};

//~mgj: Asset Store
static AssetStore*
AssetStoreCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                 U64 texture_map_size, U64 total_size_in_bytes);
static void
AssetStoreDestroy(VkDevice device, AssetStore* asset_stream);
static void
AssetStoreTextureThreadMain(async::ThreadInfo thread_info, void* data);
static AssetStoreTexture*
AssetStoreTextureGetSlot(AssetStore* asset_store, U64 texture_id);
// ~mgj: road function
static void
RoadDescriptorCreate(VkDescriptorPool desc_pool, RoadDescriptorCreateInfo* info, Texture* texture);

static Road*
RoadCreate(async::Queue* work_queue, VulkanContext* vk_ctx, city::Road* road);
static void
RoadUpdate(city::Road* road, Road* w_road, VulkanContext* vk_ctx, U32 image_index,
           String8 shader_path);
static void
RoadCleanup(city::City* city, VulkanContext* vk_ctx);

struct Vulkan_PushConstantInfo
{
    uint32_t offset;
    uint32_t size;
};

// image helpers

static void
ImageLayoutTransition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
                      VkImageLayout oldLayout, VkImageLayout newLayout, U32 mipmap_level);

static void
VK_GenerateMipmaps(VkCommandBuffer command_buffer, VkImage image, int32_t tex_width,
                   int32_t text_height, uint32_t mip_levels);

// queue family

static void
VK_DepthResourcesCreate(VulkanContext* vk_context, SwapchainResources* swapchain_resources);

static void
VK_RecreateSwapChain(IO* io_ctx, VulkanContext* vk_ctx);

static void
VK_SyncObjectsCreate(VulkanContext* vk_ctx);

static void
SyncObjectsDestroy(VulkanContext* vk_ctx);

static VkCommandBuffer
CommandBufferCreate(VkDevice device, VkCommandPool cmd_pool);
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

static VkCommandBuffer
VK_BeginSingleTimeCommands(VkDevice device, VkCommandPool cmd_pool);

static void
VK_EndSingleTimeCommands(VulkanContext* vk_ctx, VkCommandPool cmd_pool,
                         VkCommandBuffer command_buffer, VkFence fence);
static VulkanContext*
VK_VulkanInit(Context* ctx);
static void
VK_Cleanup(VulkanContext* vk_ctx);

// ~mgj: Threaded Graphics Queue
static GraphicsQueue
TreadedGraphicsQueueCreate(VkDevice device, U32 graphics_index);
static void
ThreadedGraphicsQueueDestroy(GraphicsQueue graphics_queue);
static void
ThreadedGraphicsQueueSubmit(GraphicsQueue graphics_queue, VkSubmitInfo* info, VkFence fence);

// ~mgj: texture functions
static void
TextureCreate(VkCommandPool cmd_pool, VkFence fence, TextureCreateInput* thread_input,
              Texture* asset_store_texture);
static void
TextureDestroy(VulkanContext* vk_ctx, Texture* texture);

static void
RoadPipelineCreate(Road* road, String8 shader_path);

static VkDescriptorSetLayout
RoadDescriptorSetLayoutCreate(VkDevice device, Road* road);
static void
RoadDescriptorSetCreate(VkDevice device, VkDescriptorPool desc_pool, Texture* texture, Road* road,
                        U32 frames_in_flight);
// ~mgj: Descriptor Related Functions
//

#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            fprintf(stderr, "Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__); \
            Trap(res == VK_SUCCESS);                                                               \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    }

} // namespace wrapper
