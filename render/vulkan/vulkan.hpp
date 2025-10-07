#pragma once

struct VK_Buffer
{
    VK_BufferAllocation buffer_alloc;
    VK_BufferAllocation staging_buffer;
};

struct VK_Texture
{
    VK_BufferAllocation staging_buffer;
    VK_ImageResource image_resource;
    VkSampler sampler;
    VkDescriptorSet desc_set;
};

static const U32 VK_MAX_FRAMES_IN_FLIGHT = 2;

struct VK_CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    VK_Frustum frustum;
    glm::vec2 viewport_dim;
};

struct VK_AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

struct VK_RoadPushConstants
{
    F32 road_height;
    F32 texture_scale;
};

struct VK_CmdQueueItem
{
    VK_CmdQueueItem* next;
    VK_CmdQueueItem* prev;
    R_ThreadInput* thread_input;
    U32 thread_id;
    VkCommandBuffer cmd_buffer;
    VkFence fence;
};

struct VK_AssetManagerCmdList
{
    Arena* arena;
    VK_CmdQueueItem* list_first;
    VK_CmdQueueItem* list_last;

    VK_CmdQueueItem* free_list;
};

template <typename T> struct VK_AssetList
{
    Arena* arena;
    R_AssetItemList<T> list;
    R_AssetItem<T>* free_list;
};

struct VK_Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
};

struct VK_AssetManager
{
    Arena* arena;

    // ~mgj: Textures
    R_AssetItemList<VK_Texture> texture_list;
    R_AssetItem<VK_Texture>* texture_free_list;

    // ~mgj: Buffers
    R_AssetItemList<VK_Buffer> buffer_list;
    R_AssetItem<VK_Buffer>* buffer_free_list;

    // ~mgj: Threading Buffer Commands
    Buffer<VK_AssetManagerCommandPool> threaded_cmd_pools;
    U64 total_size;
    async::Queue<VK_CmdQueueItem>* cmd_queue;
    async::Queue<async::QueueItem>* work_queue;
    VK_AssetManagerCmdList* cmd_wait_list;
};

struct VK_Model3DNode
{
    VK_Model3DNode* next;
    B32 depth_write_per_draw_enabled;
    VK_BufferAllocation index_alloc;
    U32 index_buffer_offset;
    U32 index_count;
    VK_BufferAllocation vertex_alloc;
    VkDescriptorSet texture_handle;
};

struct VK_Model3DInstanceNode
{
    VK_Model3DInstanceNode* next;
    VK_BufferAllocation index_alloc;
    VK_BufferAllocation vertex_alloc;
    R_BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
    VkDescriptorSet texture_handle;
};

struct VK_Model3DNodeList
{
    VK_Model3DNode* first;
    VK_Model3DNode* last;
};

struct VK_Model3DInstanceNodeList
{
    VK_Model3DInstanceNode* first;
    VK_Model3DInstanceNode* last;
};

struct VK_Model3DInstance
{
    VK_Model3DInstanceNodeList list;
    U32 total_instance_buffer_byte_count;
};

struct VK_DrawFrame
{
    VK_Model3DNodeList model_3D_list;
    VK_Model3DInstance model_3D_instance_draw;
};

struct VK_Context
{
    static const U32 WIDTH = 800;
    static const U32 HEIGHT = 600;

#ifdef BUILD_DEBUG
    static const U8 enable_validation_layers = 1;
#else
    static const U8 enable_validation_layers = 0;
#endif
    Arena* arena;

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

    VkFormat object_id_format;
    U64 hovered_object_id;
    VK_SwapchainResources* swapchain_resources;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    // queue
    VK_QueueFamilyIndices queue_family_indices;
    VkDescriptorPool descriptor_pool;
    // ~mgj: camera resources for uniform buffers
    VK_BufferAllocationMapped camera_buffer_alloc_mapped[VK_MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Asset Streaming
    VK_AssetManager* asset_manager;

    // ~mgj: Profiling
    TracyVkCtx tracy_ctx[VK_MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Font Rendering
    VkDescriptorSetLayout font_descriptor_set_layout;

    // ~mgj: Rendering
    Arena* draw_frame_arena;
    VK_DrawFrame* draw_frame;
    VK_Pipeline model_3D_pipeline;
    VK_Pipeline model_3D_instance_pipeline;
    VK_BufferAllocation model_3D_instance_buffer;
};
static void
VK_TextureDestroy(VK_Context* vk_ctx, VK_Texture* texture);
static void
VK_ThreadSetup(async::ThreadInfo thread_info, void* input);
static void
VK_TextureCreate(VkCommandBuffer cmd_buffer, R_Handle handle, String8 texture_path);

static void
VK_Model3DInstanceRendering();

//~mgj: camera functions
static void
VK_CameraCleanup(VK_Context* vk_ctx);

static void
VK_CameraUniformBufferCreate(VK_Context* vk_ctx);
static void
VK_CameraUniformBufferUpdate(VK_Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                             U32 current_frame);
static void
VK_CameraDescriptorSetLayoutCreate(VK_Context* vk_ctx);
static void
VK_CameraDescriptorSetCreate(VK_Context* vk_ctx);

// static void
// AssetManagerRoadResourceLoadAsync(AssetId texture_id, String8 texture_path,
//                                   R_SamplerInfo* sampler_info);
//~mgj: Asset Store
static VK_AssetManager*
VK_AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                      U64 total_size_in_bytes);
static void
VK_AssetManagerDestroy(VK_Context* vk_ctx, VK_AssetManager* asset_stream);
static R_AssetItem<VK_Texture>*
VK_AssetManagerTextureItemGet(R_Handle handle);
template <typename T>
static R_AssetItem<T>*
VK_AssetManagerItemCreate(Arena* arena, R_AssetItemList<T>* list, R_AssetItem<T>** free_list);
template <typename T>
static R_AssetItem<T>*
VK_AssetManagerItemGet(R_AssetItemList<T>* list, R_Handle handle);
static void
VK_AssetManagerExecuteCmds();
static void
VK_AssetManagerCmdDoneCheck();
static VkCommandBuffer
VK_BeginCommand(VkDevice device, VK_AssetManagerCommandPool threaded_cmd_pool);
static VK_AssetManagerCmdList*
VK_AssetManagerCmdListCreate();
static void
VK_AssetManagerCmdListDestroy(VK_AssetManagerCmdList* cmd_wait_list);
static void
VK_AssetManagerCmdListAdd(VK_AssetManagerCmdList* cmd_list, VK_CmdQueueItem item);
static void
VK_AssetManagerCmdListItemRemove(VK_AssetManagerCmdList* cmd_list, VK_CmdQueueItem* item);

static void
VK_AssetManagerBufferFree(R_Handle handle);
static void
VK_AssetManagerTextureFree(R_Handle handle);

static void
VK_AssetCmdQueueItemEnqueue(U32 thread_id, VkCommandBuffer cmd, R_ThreadInput* thread_input);

template <typename T>
static void
VK_AssetInfoBufferCmd(VkCommandBuffer cmd, R_Handle handle, Buffer<T> buffer);

static R_ThreadInput*
VK_ThreadInputCreate();
static void
VK_ThreadInputDestroy(R_ThreadInput* thread_input);

static void
VK_ImageFromKtx2file(VkCommandBuffer cmd, VkImage image, VK_BufferAllocation staging_buffer,
                     VK_Context* vk_ctx, ktxTexture2* ktx_texture);
// ~mgj: Vulkan Lifetime
static void
VK_CtxSet(VK_Context* vk_ctx);
static VK_Context*
VK_CtxGet();

// ~mgj: Building
static void
VK_Model3DBucketAdd(VK_BufferAllocation* vertex_buffer_allocation,
                    VK_BufferAllocation* index_buffer_allocation, VkDescriptorSet texture_handle,
                    B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count);
static void
VK_Model3DInstanceBucketAdd(VK_BufferAllocation* vertex_buffer_allocation,
                            VK_BufferAllocation* index_buffer_allocation,
                            VkDescriptorSet texture_handle, R_BufferInfo* instance_buffer_info);
static void
VK_Model3DDraw(R_Handle texture_handle, R_Handle vertex_buffer_handle, R_Handle index_buffer_handle,
               B32 depth_test_per_draw_call_only, U32 index_buffer_offset, U32 index_count);
static void
VK_Model3DInstanceDraw(R_Handle texture_handle, R_Handle vertex_buffer_handle,
                       R_Handle index_buffer_handle, R_BufferInfo* instance_buffer);
static VK_Pipeline
VK_Model3DInstancePipelineCreate(VK_Context* vk_ctx, String8 shader_path);
static VK_Pipeline
VK_Model3DPipelineCreate(VK_Context* vk_ctx, String8 shader_path);
static void
VK_DrawFrameReset();
static void
VK_Model3DRendering();
static void
VK_PipelineDestroy(VK_Pipeline* draw_ctx);

// Handle Conversions
#define VK_CHECK_RESULT(f)                                                                         \
    {                                                                                              \
        VkResult res = (f);                                                                        \
        if (res != VK_SUCCESS)                                                                     \
        {                                                                                          \
            ERROR_LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__);       \
            Trap();                                                                                \
        }                                                                                          \
    }

static void
VK_CommandBufferRecord(U32 image_index, U32 current_frame, ui::Camera* camera,
                       Vec2S64 mouse_cursor_pos);

static void
VK_ProfileBuffersCreate(VK_Context* vk_ctx);
static void
VK_ProfileBuffersDestroy(VK_Context* vk_ctx);
