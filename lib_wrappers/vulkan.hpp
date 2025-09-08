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
struct Vertex3D;
} // namespace city

namespace wrapper
{
static const U32 MAX_FRAMES_IN_FLIGHT = 2;

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

struct Car
{
    Arena* arena;
    render::AssetId texture_id;
    String8 texture_path;
    render::AssetId vertex_buffer_id;
    render::AssetId index_buffer_id;

    B32 is_pipeline_created;

    PipelineInfo pipeline_info;
    BufferContext instance_buffer_mapped;
    VkDescriptorSetLayout descriptor_set_layout;
    Buffer<VkDescriptorSet> descriptor_sets;
};

struct AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

struct RoadPushConstants
{
    F32 road_height;
    F32 texture_scale;
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    render::ThreadInput* thread_input;
    render::AssetInfoNodeList asset_list;
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
    render::AssetItemList<T> list;
    render::AssetItem<T>* free_list;
};

struct BuildingPipeline
{
    PipelineInfo pipeline_info;
    VkDescriptorSetLayout descriptor_set_layout;
};

struct AssetManager
{
    Arena* arena;

    // ~mgj: asset info
    Arena* asset_arena;
    render::AssetInfoNode* asset_free_list;

    // ~mgj: Textures
    Arena* texture_arena;
    Buffer<render::AssetItemList<Texture>> texture_hashmap;
    render::AssetItem<Texture>* texture_free_list;

    // ~mgj: Buffers
    Arena* buffer_arena;
    render::AssetItemList<AssetItemBuffer> buffer_list;
    render::AssetItem<AssetItemBuffer>* buffer_free_list;

    // ~mgj: Threading Buffer Commands
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
    VkDescriptorSet descriptor_set;
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

#ifdef BUILD_DEBUG
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
    BuildingPipeline building_pipeline;
};

// ~mgj: Car functions
static Car*
CarCreate(render::AssetId texture_id, String8 texture_path, CgltfSampler sampler,
          Buffer<city::CarVertex> vertex_buffer, Buffer<U32> index_buffer);
static void
CarDestroy(wrapper::VulkanContext* vk_ctx, wrapper::Car* car);
static void
CarUpdate(city::CarSim* car_sim, Buffer<city::CarInstance> instance_buffer, U32 image_idx);
// static void
// CarCreateAsync(AssetId texture_id, AssetId vertex_buffer_id, AssetId index_buffer_id,
//                String8 texture_path, render::SamplerInfo* sampler,
//                Buffer<city::CarVertex> vertex_buffer, Buffer<U32> index_buffer);

static void
ThreadSetup(async::ThreadInfo thread_info, void* input);
static void
TextureCreate(VkCommandBuffer cmd_buffer, render::AssetInfo asset_info, String8 texture_path,
              render::SamplerInfo sampler_info);

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

// static void
// AssetManagerRoadResourceLoadAsync(AssetId texture_id, String8 texture_path,
//                                   render::SamplerInfo* sampler_info);
//~mgj: Asset Store
static AssetManager*
AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                   U64 texture_map_size, U64 total_size_in_bytes);
static void
AssetManagerDestroy(VulkanContext* vk_ctx, AssetManager* asset_stream);
static render::AssetItem<Texture>*
AssetManagerTextureItemGet(render::AssetId asset_id);
template <typename T>
static render::AssetItem<T>*
AssetManagerItemGet(Arena* arena, render::AssetItemList<T>* list, render::AssetItem<T>** free_list,
                    render::AssetId id);
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

force_inline static U64
HashIndexFromAssetId(render::AssetId id, U64 hashmap_size);
force_inline static B32
AssetIdCmp(render::AssetId a, render::AssetId b);
static render::AssetItem<AssetItemBuffer>*
AssetManagerBufferItemGet(render::AssetId id);
static void
AssetInfoNodeListAdd(Arena* arena, render::AssetInfoNodeList* node_list, render::AssetInfo info);
static void
AssetManagerAssetInfoAdd(render::AssetInfoNodeList* asset_list, render::AssetInfo asset_info);
static void
AssetManagerAssetInfoAddMany(render::AssetInfoNodeList* asset_list, render::AssetInfo* asset_arr,
                             U32 count);
static void
AssetManagerAssetInfoRemove(render::AssetInfoNodeList* asset_list);
static void
AssetManagerBufferFree(render::AssetId asset_id);
static void
AssetManagerTextureFree(render::AssetId asset_id);

static void
AssetCmdQueueItemEnqueue(render::AssetInfoNodeList* asset_node_list, U32 thread_id,
                         VkCommandBuffer cmd, render::ThreadInput* thread_input);
static void
AssetItemBufferDestroy(VmaAllocator allocator, render::AssetItem<AssetItemBuffer>* asset_buffer);
template <typename T>
static render::AssetInfo
AssetInfoBufferCmd(VkCommandBuffer cmd, render::AssetId id, Buffer<T> vertex_buffer,
                   VkBufferUsageFlagBits usage_flags);
static void
AssetTextureLoad(Arena* arena, render::AssetItem<Texture> asset_item,
                 render::AssetLoadingInfoNodeList* asset_loading_wait_list,
                 render::AssetInfo* asset_info, render::SamplerInfo* sampler_info,
                 String8 texture_path);

static void
AssetBufferLoad(Arena* arena, render::AssetItem<AssetItemBuffer>* asset_item,
                render::AssetLoadingInfoNodeList* asset_loading_wait_list,
                render::AssetInfo* asset_info, render::BufferInfo* buffer_info);
static render::ThreadInput*
ThreadInputCreate();
static void
ThreadInputDestroy(render::ThreadInput* thread_input);

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
BuildingCreateAsync(render::AssetId vertex_buffer_id, render::AssetId index_buffer_id,
                    Buffer<city::Vertex3D> vertex_buffer, Buffer<U32> index_buffer);
static void
BuildingBucketAdd(BufferAllocation* vertex_buffer_allocation,
                  BufferAllocation* index_buffer_allocation, VkDescriptorSet desc_set);

static void
BuildingDraw(render::AssetInfo* vertex_info, render::AssetInfo* index_info,
             render::AssetInfo* texture_info, String8 texture_path,
             render::SamplerInfo* sampler_info, render::BufferInfo* vertex_buffer_info,
             render::BufferInfo* index_buffer_info);
static void
BuildingPipelineCreate();
static PipelineInfo
BuildingPipelineInfoCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout desc_set_layout);
static void
DrawFrameReset();
static void
BuildingRendering();
static void
BuildingPipelineDestroy(BuildingPipeline* draw_ctx);
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
