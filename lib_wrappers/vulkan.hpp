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
struct BuildingVertex;
} // namespace city

namespace wrapper
{
static const U32 MAX_FRAMES_IN_FLIGHT = 2;
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

struct ModelThreadInput
{
    AssetId vertex_buffer_id;
    AssetId index_buffer_id;
    SamplerInfo sampler_info;
    Buffer<city::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;
};

struct BuildingThreadInput
{
    AssetId vertex_buffer_id;
    AssetId index_buffer_id;
    Buffer<city::BuildingVertex> vertex_buffer;
    Buffer<U32> index_buffer;
};

enum ThreadInputType
{
    ThreadInputType_Building,
    ThreadInputType_Road,
    ThreadInputType_Model,
    ThreadInputType_Count
};

struct ThreadInput
{
    AssetId texture_id;
    String8 texture_path;
    SamplerInfo sampler_info;

    ThreadInputType type;
    union
    {
        ModelThreadInput model_input;
        BuildingThreadInput building_input;
    } type_data;
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

struct AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

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
    AssetId id;
    AssetItemType type;
};

struct AssetInfoNode
{
    AssetInfoNode* next;
    AssetInfo info;
};

struct AssetInfoNodeList
{
    AssetInfoNode* first;
    AssetInfoNode* last;
    U64 count;
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    AssetInfoNodeList asset_list;
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

template <typename T> struct AssetList
{
    Arena* arena;
    AssetItemList<T> list;
    AssetItem<T>* free_list;
};

struct Building
{
    PipelineInfo pipeline_info;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
};

struct AssetManager
{
    Arena* arena;

    // ~mgj: asset info
    Arena* asset_arena;
    AssetInfoNode* asset_free_list;

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

struct BuildingNode
{
    BuildingNode* next;
    BufferAllocation index_alloc;
    BufferAllocation vertex_alloc;
};

struct BuildingNodeList
{
    BuildingNode* first;
    BuildingNode* last;
};

struct DrawFrame
{
    BuildingNodeList building_list;
};

struct VulkanContext
{
    static const U32 WIDTH = 800;
    static const U32 HEIGHT = 600;

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
    AssetManager* asset_manager;

    // ~mgj: Profiling
    TracyVkCtx tracy_ctx[MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Rendering

    Arena* draw_frame_arena;
    DrawFrame* draw_frame;
    Building building_draw;
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
               String8 texture_path, SamplerInfo* sampler, Buffer<city::CarVertex> vertex_buffer,
               Buffer<U32> index_buffer);

static void
ThreadSetup(async::ThreadInfo thread_info, void* input);
static AssetInfo
TextureCreate(VkCommandBuffer cmd_buffer, AssetId texture_id, String8 texture_path,
              SamplerInfo sampler_info);

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
// static void
// RoadTextureCreate(U32 thread_id, AssetManagerCommandPool thread_cmd_pool,
//                   RoadTextureCreateInput* input, Texture* texture);
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
AssetManagerRoadResourceLoadAsync(AssetId texture_id, String8 texture_path,
                                  SamplerInfo* sampler_info);
//~mgj: Asset Store
static AssetManager*
AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                   U64 texture_map_size, U64 total_size_in_bytes);
static void
AssetManagerDestroy(VulkanContext* vk_ctx, AssetManager* asset_stream);
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
static void
AssetInfoNodeListAdd(Arena* arena, AssetInfoNodeList* node_list, AssetInfo info);
static void
AssetManagerAssetInfoAdd(AssetInfoNodeList* asset_list, AssetInfo asset_info);
static void
AssetManagerAssetInfoAddMany(AssetInfoNodeList* asset_list, AssetInfo* asset_arr, U32 count);
static void
AssetManagerAssetInfoRemove(AssetInfoNodeList* asset_list);
static void
AssetManagerBufferFree(AssetId asset_id);

static void
AssetCmdQueueItemEnqueue(AssetInfoNodeList* asset_node_list, U32 thread_id, VkCommandBuffer cmd);
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

// ~mgj: Building
static void
BuildingCreateAsync(AssetId vertex_buffer_id, AssetId index_buffer_id,
                    Buffer<city::BuildingVertex> vertex_buffer, Buffer<U32> index_buffer);
static void
BuildingBucketAdd(BufferAllocation* vertex_buffer_allocation,
                  BufferAllocation* index_buffer_allocation);
static void
BuildingDraw(AssetId vertex_buffer_id, AssetId index_buffer_id);
static PipelineInfo
BuildingPipelineInfoCreate(VulkanContext* vk_ctx);
static void
DrawFrameReset();
static void
BuildingRendering();
static void
BuildingPipelineDestroy(Building* draw_ctx);
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
