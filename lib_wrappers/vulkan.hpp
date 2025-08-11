#pragma once

namespace wrapper
{
struct VulkanContext;
}
namespace ui
{
struct Camera;
}
namespace city
{
struct Road;
struct CarSim;
struct CarInstance;
} // namespace city

namespace wrapper
{

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
};

struct Car
{
    Arena* arena;

    BufferAllocation vertex_buffer_alloc;
    BufferAllocation index_buffer_alloc;

    PipelineInfo pipeline_info;
    BufferContext instance_buffer_mapped;
    Texture* texture;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
};
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
struct RoadTextureCreateInput
{
    VulkanContext* vk_ctx;
    Road* w_road;
};

typedef void(TextureCreateFunc)(VkCommandPool cmd_pool, VkFence fence,
                                RoadTextureCreateInput* input, Texture* texture);
struct RoadDescriptorCreateInfo
{
    VulkanContext* vk_ctx;
    city::Road* road;
    Road* w_road;
};

struct RoadPushConstants
{
    F32 road_height;
    F32 texture_scale;
};
struct AssetStoreTexture
{
    AssetStoreTexture* next;
    U64 id;
    B32 is_loaded;
    Texture asset;
};

struct RoadThreadInput
{
    AssetStoreTexture* asset_store_texture;

    TextureCreateFunc* texture_func;
    RoadTextureCreateInput* texture_input;

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

    // queue
    QueueFamilyIndices queue_family_indices;

    VkDescriptorPool descriptor_pool;
    // ~mgj: camera resources for uniform buffers
    BufferAllocationMapped camera_buffer_alloc_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Asset Streaming
    AssetStore* asset_store;
};

// ~mgj: Car functions
static Car*
CarCreate(VulkanContext* vk_ctx, CgltfSampler sampler, Buffer<city::CarVertex> vertex_buffer,
          Buffer<U32> index_buffer);
static void
CarDestroy(wrapper::VulkanContext* vk_ctx, wrapper::Car* car);
static void
CarUpdate(VulkanContext* vk_ctx, Car* w_car, Buffer<city::CarInstance> instance_buffer);
static PipelineInfo
CarPipelineCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout layout);

static void
CarRendering(VulkanContext* vk_ctx, city::CarSim* car, U32 image_idx, U32 instance_count);

//~mgj: camera functions
static void
CameraCleanup(VulkanContext* vk_ctx);

static void
CameraUniformBufferCreate(VulkanContext* vk_ctx);
static void
CameraUniformBufferUpdate(VulkanContext* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                          U32 current_frame);
static void
CameraDescriptorSetLayoutCreate(VulkanContext* vk_ctx);
static void
CameraDescriptorSetCreate(VulkanContext* vk_ctx);

// ~mgj: road function
static void
RoadDescriptorCreate(VkDescriptorPool desc_pool, RoadDescriptorCreateInfo* info, Texture* texture);
static Road*
RoadCreate(VulkanContext* vk_ctx, city::Road* road);
static void
RoadUpdate(city::Road* road, VulkanContext* vk_ctx, U32 image_index, String8 shader_path);
static void
RoadTextureCreate(VkCommandPool cmd_pool, VkFence fence, RoadTextureCreateInput* thread_input,
                  Texture* asset_store_texture);
static void
RoadDestroy(city::Road* road, VulkanContext* vk_ctx);

// ~mgj: Road Functions
static void
RoadPipelineCreate(Road* road, String8 shader_path);
static VkDescriptorSetLayout
RoadDescriptorSetLayoutCreate(VkDevice device, Road* road);
static void
RoadDescriptorSetCreate(VkDevice device, VkDescriptorPool desc_pool, Texture* texture, Road* road,
                        U32 frames_in_flight);

static VkVertexInputBindingDescription
RoadBindingDescriptionGet();
static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena);
static void
AssetStoreRoadResourceLoadAsync(AssetStore* asset_store, AssetStoreTexture* texture,
                                VulkanContext* vk_ctx, String8 shader_path, Road* w_road,
                                city::Road* road);
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

// ~mgj: Vulkan Lifetime
static VulkanContext*
VK_VulkanInit(Context* ctx);
static void
VK_Cleanup(VulkanContext* vk_ctx);

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
