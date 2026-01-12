#pragma once

namespace vulkan
{

struct BufferUpload
{
    BufferAllocation buffer_alloc;
    BufferAllocation staging_buffer;
};

struct Texture
{
    BufferAllocation staging_buffer;
    ImageResource image_resource;
    VkSampler sampler;
    VkDescriptorSetLayout desc_set_layout;
    VkDescriptorSet desc_set;
};

static const U32 MAX_FRAMES_IN_FLIGHT = 2;

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
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
    U32 thread_id;
    VkCommandBuffer cmd_buffer;
    VkFence fence;
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

struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
};

struct PendingDeletion
{
    PendingDeletion* next;
    render::Handle handle;
    U64 frame_to_delete;
    render::AssetItemType type;
};

struct DeletionQueue
{
    PendingDeletion* first;
    PendingDeletion* last;
    PendingDeletion* free_list;
    U64 frame_counter;
};

struct AssetManager
{
    Arena* arena;

    // ~mgj: Textures
    render::AssetItemList<Texture> texture_list;
    render::AssetItem<Texture>* texture_free_list;

    // ~mgj: Buffers
    render::AssetItemList<BufferUpload> buffer_list;
    render::AssetItem<BufferUpload>* buffer_free_list;

    // ~mgj: Threading Buffer Commands
    ::Buffer<AssetManagerCommandPool> threaded_cmd_pools;
    U64 total_size;
    async::Queue<CmdQueueItem>* cmd_queue;
    async::Queue<async::QueueItem>* work_queue;
    AssetManagerCmdList* cmd_wait_list;

    // ~mgj: Deferred Deletion Queue
    DeletionQueue* deletion_queue;
};

struct Model3DNode
{
    Model3DNode* next;
    B32 depth_write_per_draw_enabled;
    BufferAllocation index_alloc;
    U32 index_buffer_offset;
    U32 index_count;
    BufferAllocation vertex_alloc;
    VkDescriptorSet texture_handle;
};

struct Model3DInstanceNode
{
    Model3DInstanceNode* next;
    BufferAllocation index_alloc;
    BufferAllocation vertex_alloc;
    render::BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
    VkDescriptorSet texture_handle;
};

struct Model3DNodeList
{
    Model3DNode* first;
    Model3DNode* last;
};

struct Model3DInstanceNodeList
{
    Model3DInstanceNode* first;
    Model3DInstanceNode* last;
};

struct Model3DInstance
{
    Model3DInstanceNodeList list;
    U32 total_instance_buffer_byte_count;
};

struct DrawFrame
{
    Model3DNodeList model_3D_list;
    Model3DInstance model_3D_instance_draw;
};

struct Context
{
    static const U32 WIDTH = 800;
    static const U32 HEIGHT = 600;

#if BUILD_DEBUG
    static const U8 enable_validation_layers = 1;
#else
    static const U8 enable_validation_layers = 0;
#endif
    Arena* arena;
    U32 render_thread_id;

    ::Buffer<String8> validation_layers;
    ::Buffer<String8> device_extensions;

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
    ::Buffer<VkCommandBuffer> command_buffers;

    ::Buffer<VkFence> in_flight_fences;
    U32 current_frame;
    U32 cur_img_idx;

    VkFormat object_id_format;
    U64 hovered_object_id;
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

    // ~mgj: Font Rendering
    VkDescriptorSetLayout font_descriptor_set_layout;

    // ~mgj: Rendering
    Arena* draw_frame_arena;
    DrawFrame* draw_frame;
    Pipeline model_3D_pipeline;
    Pipeline model_3D_instance_pipeline;
    BufferAllocation model_3D_instance_buffer;
};

static void
texture_destroy(Texture* texture);
static void
thread_setup(async::ThreadInfo thread_info, void* input);

g_internal void
blit_transition_image(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout src_layout,
                      VkImageLayout dst_layout, U32 mip_level);
g_internal void
texture_ktx_cmd_record(VkCommandBuffer cmd, Texture* tex, ::Buffer<U8> tex_buf);
g_internal B32
texture_cmd_record(VkCommandBuffer cmd, Texture* tex, ::Buffer<U8> tex_buf);

static void
model_3d_instance_rendering();

//~mgj: camera functions
static void
camera_cleanup(Context* vk_ctx);

static void
camera_uniform_buffer_create(Context* vk_ctx);
static void
camera_uniform_buffer_update(Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                             U32 current_frame);
static void
camera_descriptor_set_layout_create(Context* vk_ctx);
static void
camera_descriptor_set_create(Context* vk_ctx);

//~mgj: Asset Manager

g_internal AssetManager*
asset_manager_get();
static AssetManager*
asset_manager_create(VkDevice device, U32 queue_family_index, async::Threads* threads,
                     U64 total_size_in_bytes);
static void
asset_manager_destroy(Context* vk_ctx, AssetManager* asset_stream);

//~mgj: Deferred Deletion Queue
static void
deletion_queue_push(DeletionQueue* queue, render::Handle handle, render::AssetItemType type,
                    U64 frames_in_flight);
static void
deletion_queue_deferred_resource_deletion(DeletionQueue* queue);
static void
deletion_queue_resource_free(PendingDeletion* deletion);
static void
deletion_queue_delete_all(DeletionQueue* queue);

static render::AssetItem<Texture>*
asset_manager_texture_item_get(render::Handle handle);
template <typename T>
static render::AssetItem<T>*
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItem<T>** free_list);
template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::AssetItemList<T>* list, render::Handle handle);
static void
asset_manager_execute_cmds();
static void
asset_manager_cmd_done_check();
static VkCommandBuffer
begin_command(VkDevice device, AssetManagerCommandPool threaded_cmd_pool);
static AssetManagerCmdList*
asset_manager_cmd_list_create();
static void
asset_manager_cmd_list_destroy(AssetManagerCmdList* cmd_wait_list);
static void
asset_manager_cmd_list_add(AssetManagerCmdList* cmd_list, CmdQueueItem item);
static void
asset_manager_cmd_list_item_remove(AssetManagerCmdList* cmd_list, CmdQueueItem* item);

static void
asset_manager_buffer_free(render::Handle handle);
static void
asset_manager_texture_free(render::Handle handle);

static void
asset_cmd_queue_item_enqueue(U32 thread_id, VkCommandBuffer cmd, render::ThreadInput* thread_input);

template <typename T>
static void
asset_info_buffer_cmd(VkCommandBuffer cmd, render::Handle handle, ::Buffer<T> buffer);

static render::ThreadInput*
thread_input_create();
static void
thread_input_destroy(render::ThreadInput* thread_input);

g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle,
                                 ::Buffer<U8> tex_buf);

// ~mgj: Vulkan Lifetime
static void
ctx_set(Context* vk_ctx);
g_internal Context*
ctx_get();

// ~mgj: Building
static void
model_3d_bucket_add(BufferAllocation* vertex_buffer_allocation,
                    BufferAllocation* index_buffer_allocation, VkDescriptorSet texture_handle,
                    B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count);
static void
model_3d_instance_bucket_add(BufferAllocation* vertex_buffer_allocation,
                             BufferAllocation* index_buffer_allocation,
                             VkDescriptorSet texture_handle,
                             render::BufferInfo* instance_buffer_info);
static Pipeline
model_3d_instance_pipeline_create(Context* vk_ctx, String8 shader_path);
static Pipeline
model_3d_pipeline_create(Context* vk_ctx, String8 shader_path);
static void
draw_frame_reset();
static void
model_3d_rendering();
static void
pipeline_destroy(Pipeline* draw_ctx);

static void
command_buffer_record(U32 image_index, U32 current_frame, ui::Camera* camera,
                      Vec2S64 mouse_cursor_pos);

static void
profile_buffers_create(Context* vk_ctx);
static void
profile_buffers_destroy(Context* vk_ctx);

} // namespace vulkan

// Handle Conversions
#undef VK_CHECK_RESULT
#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            ERROR_LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__);       \
            Trap();                                                                                \
        }                                                                                          \
    }
