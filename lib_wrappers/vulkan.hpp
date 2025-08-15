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
struct AssetId
{
    U64 id;
};
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
    AssetId texture_id;
    String8 texture_path;
    CgltfSampler sampler;
};

struct Car
{
    Arena* arena;
    AssetId texture_id;
    String8 texture_path;

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
    BufferContext vertex_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
    B32 descriptors_are_created;
    String8 road_texture_path;
    AssetId texture_id;
};
struct RoadTextureCreateInput
{
    VulkanContext* vk_ctx;
    Road* w_road;
};

struct AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

typedef void(TextureCreateFunc)(U32 thread_id, AssetManagerCommandPool thread_cmd_pool,
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

struct AssetItemTexture
{
    AssetItemTexture* next;
    AssetId id;
    B32 is_loaded;
    Texture asset;
};

struct RoadThreadInput
{
    AssetItemTexture* asset_store_texture;

    TextureCreateFunc* texture_func;
    RoadTextureCreateInput* texture_input;

    Buffer<AssetManagerCommandPool> threaded_cmd_pools;
};

struct AssetItemTextureList
{
    AssetItemTexture* first;
    AssetItemTexture* last;
};

enum AssetItemType
{
    AssetItemType_Texture,
    AssetItemType_Buffer
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    AssetId asset_id;
    AssetItemType asset_type;
    U32 thread_id;
    VkCommandBuffer cmd_buffer;
    VkFence fence;
};

struct AssetItemBuffer
{
    U128 id; // create from shader path
    B32 is_loaded;
    BufferAllocation buffer_alloc;
    BufferAllocation staging_buffer;
};

struct AssetItemBufferList
{
    AssetItemBuffer* first;
    AssetItemBuffer* last;
};

struct AssetManagerCmdList
{
    Arena* arena;
    CmdQueueItem* list_first;
    CmdQueueItem* list_last;

    CmdQueueItem* free_list;
};

struct AssetManager
{
    Arena* arena;

    // ~mgj: Textures
    Buffer<AssetItemTextureList> hashmap;
    AssetItemTexture* free_list;

    // ~mgj: Buffers
    AssetItemBufferList buffer_list;
    AssetItemTexture buffer_free_list;

    // ~mgj: Threading related functionality
    Buffer<AssetManagerCommandPool> threaded_cmd_pools;
    U64 total_size;
    async::Queue<CmdQueueItem>* cmd_queue;
    async::Queue<async::QueueItem>* work_queue;
    AssetManagerCmdList* cmd_wait_list;
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
    AssetManager* asset_store;
};

// ~mgj: Car functions
static Car*
CarCreate(AssetId texture_id, String8 texture_path, CgltfSampler sampler,
          Buffer<city::CarVertex> vertex_buffer, Buffer<U32> index_buffer);
static void
CarDestroy(wrapper::VulkanContext* vk_ctx, wrapper::Car* car);
static void
CarUpdate(city::CarSim* car_sim, Buffer<city::CarInstance> instance_buffer, U32 image_idx);
static void
CarTextureCreate(AssetId texture_id, String8 texture_path, CgltfSampler sampler);

static void
CarTextureThreadSetup(async::ThreadInfo thread_info, void* input);
static void
CarTextureCreateWorker(U32 thread_id, AssetManagerCommandPool cmd_pool, AssetId texture_id,
                       String8 texture_path, CgltfSampler sampler);

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
RoadTextureCreate(U32 thread_id, AssetManagerCommandPool thread_cmd_pool,
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
AssetManagerRoadResourceLoadAsync(AssetManager* asset_store, AssetItemTexture* texture,
                                  VulkanContext* vk_ctx, String8 shader_path, Road* w_road,
                                  city::Road* road);
//~mgj: Asset Store
static AssetManager*
AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                   U64 texture_map_size, U64 total_size_in_bytes);
static void
AssetManagerDestroy(VulkanContext* vk_ctx, AssetManager* asset_stream);
static void
AssetManagerTextureThreadMain(async::ThreadInfo thread_info, void* data);
static AssetItemTexture*
AssetManagerTextureGetSlot(AssetId asset_id);
static void
AssetManagerExecuteCmds();
static void
AssetManagerCmdDoneCheck();
static VkCommandBuffer
BeginCommand(VkDevice device, AssetManagerCommandPool threaded_cmd_pool);
static AssetManagerCmdList*
AssetManagerCmdListCreate();
static void
AssetManagerCmdListDestroy(AssetManagerCmdList* cmd_wait_list);
static void
AssetManagerCmdListAdd(AssetManagerCmdList* cmd_list, CmdQueueItem item);
static void
AssetManagerCmdListRemove(AssetManagerCmdList* cmd_list, CmdQueueItem* item);
static AssetId
AssetIdFromStr8(String8 str);
force_inline static U64
HashIndexFromAssetId(AssetId id, U64 hashmap_size);
force_inline static B32
AssetIdCmp(AssetId a, AssetId b);
static void
AssetManagerBufferItemGet();
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
