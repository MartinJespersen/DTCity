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
struct ImageKtx2
{
    ImageResource image_resource;
    S32 width;
    S32 height;
    U32 mip_level_count;
};

struct CarThreadInput
{
    String8 texture_id;
    CgltfSampler sampler;
};

struct Car
{
    Arena* arena;
    String8 texture_id;
    B32 is_pipeline_created;

    BufferAllocation vertex_buffer_alloc;
    BufferAllocation index_buffer_alloc;

    PipelineInfo pipeline_info;
    BufferContext instance_buffer_mapped;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
};
struct Road
{
    Arena* arena;
    String8 texture_id;
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

struct AssetStoreCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

typedef void(TextureCreateFunc)(U32 thread_id, AssetStoreCommandPool thread_cmd_pool,
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

struct AssetStoreItem
{
    AssetStoreItem* next;
    String8 id;
    B32 is_loaded;
    Texture asset;
};

struct RoadThreadInput
{
    AssetStoreItem* asset_store_texture;

    TextureCreateFunc* texture_func;
    RoadTextureCreateInput* texture_input;

    Buffer<AssetStoreCommandPool> threaded_cmd_pools;
};

struct AssetStoreItemStateList
{
    AssetStoreItem* first;
    AssetStoreItem* last;
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    String8 asset_id;
    U32 thread_id;
    VkCommandBuffer cmd_buffer;
    VkFence fence;
};

struct AssetStoreCmdList
{
    Arena* arena;
    CmdQueueItem* list_first;
    CmdQueueItem* list_last;

    CmdQueueItem* free_list;
};

struct AssetStore
{
    Arena* arena;
    async::Queue<CmdQueueItem>* cmd_queue;
    async::Queue<async::QueueItem>* work_queue;
    Buffer<AssetStoreItemStateList> hashmap;
    Buffer<AssetStoreCommandPool> threaded_cmd_pools;
    AssetStoreItem* free_list;
    U64 total_size;
    U64 used_size;
    AssetStoreCmdList* cmd_wait_list;
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
    VkQueue graphics_queue;
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
CarCreate(String8 texture_id, CgltfSampler sampler, Buffer<city::CarVertex> vertex_buffer,
          Buffer<U32> index_buffer);
static void
CarDestroy(wrapper::VulkanContext* vk_ctx, wrapper::Car* car);
static void
CarUpdate(city::CarSim* car_sim, Buffer<city::CarInstance> instance_buffer, U32 image_idx);
static void
CarTextureCreate(String8 texture_id, CgltfSampler sampler);

static void
CarTextureThreadSetup(async::ThreadInfo thread_info, void* input);
static void
CarTextureCreateWorker(U32 thread_id, AssetStoreCommandPool cmd_pool, String8 texture_id,
                       CgltfSampler sampler);

static PipelineInfo
CarPipelineInfoCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout layout);

static void
CarCreateDescriptorSetLayout();
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
RoadTextureCreate(U32 thread_id, AssetStoreCommandPool thread_cmd_pool,
                  RoadTextureCreateInput* input, Texture* texture);
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
AssetStoreRoadResourceLoadAsync(AssetStore* asset_store, AssetStoreItem* texture,
                                VulkanContext* vk_ctx, String8 shader_path, Road* w_road,
                                city::Road* road);
//~mgj: Asset Store
static AssetStore*
AssetStoreCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                 U64 texture_map_size, U64 total_size_in_bytes);
static void
AssetStoreDestroy(VulkanContext* vk_ctx, AssetStore* asset_stream);
static void
AssetStoreTextureThreadMain(async::ThreadInfo thread_info, void* data);
static AssetStoreItem*
AssetStoreTextureGetSlot(String8 asset_id);
static void
AssetStoreExecuteCmds();
static void
AssetStoreCmdDoneCheck();
static VkCommandBuffer
BeginCommand(VkDevice device, AssetStoreCommandPool threaded_cmd_pool);
static AssetStoreCmdList*
AssetStoreCmdListCreate();
static void
AssetStoreCmdListDestroy(AssetStoreCmdList* cmd_wait_list);
static void
AssetStoreCmdListAdd(AssetStoreCmdList* cmd_list, CmdQueueItem item);
static void
AssetStoreCmdListRemove(AssetStoreCmdList* cmd_list, CmdQueueItem* item);
static U64
AssetStoreHash(String8 str);
// ~mgj: Vulkan Lifetime
static VulkanContext*
VK_VulkanInit(Context* ctx);
static void
VK_Cleanup(VulkanContext* vk_ctx);
static void
VulkanCtxSet(VulkanContext* vk_ctx);
static VulkanContext*
VulkanCtxGet();

#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            fprintf(stderr, "Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__); \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    }

} // namespace wrapper
