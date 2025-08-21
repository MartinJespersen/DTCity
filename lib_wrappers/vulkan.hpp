#pragma once

struct Context;
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
    AssetId vertex_buffer_id;
    AssetId index_buffer_id;
    String8 texture_path;
    CgltfSampler sampler;
    Buffer<city::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;
};

struct Car
{
    Arena* arena;
    AssetId texture_id;
    String8 texture_path;
    AssetId vertex_buffer_id;
    AssetId index_buffer_id;

    B32 is_pipeline_created;

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
    AssetId texture_id;
    String8 texture_path;
};

struct AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

typedef void(TextureCreateFunc)(U32 thread_id, AssetManagerCommandPool thread_cmd_pool,
                                RoadTextureCreateInput* input);
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

template <typename T> struct AssetItem
{
    AssetItem* next;
    AssetId id;
    B32 is_loaded;
    T item;
};

struct RoadThreadInput
{
    TextureCreateFunc* texture_func;
    RoadTextureCreateInput* texture_input;

    Buffer<AssetManagerCommandPool> threaded_cmd_pools;
};

template <typename T> struct AssetItemList
{
    AssetItem<T>* first;
    AssetItem<T>* last;
};

enum AssetItemType
{
    AssetItemType_Texture,
    AssetItemType_Buffer
};
struct AssetInfo
{
    AssetInfo* next;
    AssetId id;
    AssetItemType type;
};

struct AssetInfoList
{
    AssetInfo* first;
    AssetInfo* last;
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    AssetInfoList asset_list;
    U32 thread_id;
    VkCommandBuffer cmd_buffer;
    VkFence fence;
};

struct AssetItemBuffer
{
    BufferAllocation buffer_alloc;
    BufferAllocation staging_buffer;
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

    // ~mgj: asset info
    Arena* asset_arena;
    AssetInfo* asset_free_list;

    // ~mgj: Textures
    Arena* texture_arena;
    Buffer<AssetItemList<Texture>> texture_hashmap;
    AssetItem<Texture>* texture_free_list;

    // ~mgj: Buffers
    Arena* buffer_arena;
    AssetItemList<AssetItemBuffer> buffer_list;
    AssetItem<AssetItemBuffer>* buffer_free_list;

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
#ifdef DEBUG_BUILD
    static const U8 enable_validation_layers = 1;
#else
    static const U8 enable_validation_layers = 0;
#endif
    Arena* arena;

    String8 texture_path;
    String8 shader_path;

    Buffer<String8> validation_layers;
    Buffer<String8> device_extensions;

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

    // ~mgj: Profiling
    TracyVkCtx tracy_ctx[MAX_FRAMES_IN_FLIGHT];
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
CarCreateAsync(AssetId texture_id, AssetId vertex_buffer_id, AssetId index_buffer_id,
               String8 texture_path, CgltfSampler sampler, Buffer<city::CarVertex> vertex_buffer,
               Buffer<U32> index_buffer);

static void
CarThreadSetup(async::ThreadInfo thread_info, void* input);
static AssetInfo
TextureCreate(VkCommandBuffer cmd_buffer, AssetId texture_id, String8 texture_path,
              CgltfSampler sampler);

static PipelineInfo
CarPipelineInfoCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout layout);

static void
CarCreateDescriptorSetLayout();
static void
CarRendering(VulkanContext* vk_ctx, city::CarSim* car, U32 instance_count,
             BufferAllocation vertex_buffer_alloc, BufferAllocation index_buffer_alloc);

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
AssetManagerRoadResourceLoadAsync(AssetManager* asset_store, AssetId texture_id,
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
static AssetItem<Texture>*
AssetManagerTextureGetSlot(AssetId asset_id);
template <typename T>
static AssetItem<T>*
AssetManagerItemGet(Arena* arena, AssetItemList<T>* list, AssetItem<T>** free_list, AssetId id);
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
AssetManagerCmdListItemRemove(AssetManagerCmdList* cmd_list, CmdQueueItem* item);
static AssetId
AssetIdFromStr8(String8 str);
force_inline static U64
HashIndexFromAssetId(AssetId id, U64 hashmap_size);
force_inline static B32
AssetIdCmp(AssetId a, AssetId b);
static AssetItem<AssetItemBuffer>*
AssetManagerBufferItemGet(AssetId id);
static AssetInfoList
AssetManagerAssetInfoAdd(AssetInfo* asset, U64 asset_count);
static void
AssetManagerAssetInfoRemove(AssetInfoList* asset_list);
static void
AssetCmdQueueItemEnqueue(AssetInfo* assets, U64 asset_count, U32 thread_id, VkCommandBuffer cmd);
static void
AssetItemBufferDestroy(VmaAllocator allocator, AssetItem<AssetItemBuffer>* asset_buffer);
template <typename T>
static AssetInfo
AssetInfoBufferCmd(VkCommandBuffer cmd, AssetId id, Buffer<T> vertex_buffer,
                   VkBufferUsageFlagBits usage_flags);
static ImageKtx2*
ImageFromKtx2file(VkCommandBuffer cmd, BufferAllocation staging_buffer, VulkanContext* vk_ctx,
                  ktxTexture2* ktx_texture);
// ~mgj: Vulkan Lifetime
static VulkanContext*
VK_VulkanInit(Context* ctx);
static void
VK_Cleanup(Context* ctx, VulkanContext* vk_ctx);
static void
VulkanCtxSet(VulkanContext* vk_ctx);
static VulkanContext*
VulkanCtxGet();

#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            ERROR_LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__);       \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    }

} // namespace wrapper
