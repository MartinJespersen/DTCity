#pragma once

namespace vulkan
{

static const U32 MAX_FRAMES_IN_FLIGHT = 2;

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
};

struct RoadPushConstants
{
    F32 road_height;
    F32 texture_scale;
};

struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
};

struct Model3dPushConstants
{
    U32 tex_idx;
};

struct Model3DNode
{
    Model3DNode* next;
    B32 depth_write_per_draw_enabled;
    BufferAllocation index_alloc;
    U32 index_buffer_offset;
    U32 index_count;
    BufferAllocation vertex_alloc;
    Model3dPushConstants push_constants;
};

struct Model3dInstancePushConstants
{
    U32 tex_idx;
};

struct Model3DInstanceNode
{
    Model3DInstanceNode* next;
    BufferAllocation index_alloc;
    BufferAllocation vertex_alloc;
    render::BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
    Model3dInstancePushConstants push_constants;
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

struct Blend3dPushConstants
{
    U32 texture_index;
    U32 colormap_index;
};

struct Blend3DNode
{
    Blend3DNode* next;
    BufferAllocation index_alloc;
    BufferAllocation vertex_alloc;
    Blend3dPushConstants push_constants;
};

struct Blend3DList
{
    Blend3DNode* first;
    Blend3DNode* last;
};

struct DrawFrame
{
    Model3DNodeList model_3D_list;
    Model3DInstance model_3D_instance_draw;
    Blend3DList blend_3d_list;
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

    Buffer<String8> validation_layers;
    Buffer<String8> device_extensions;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkFormat blit_format;

    VkQueue graphics_queue;
    VkSurfaceKHR surface;
    VkQueue present_queue;

    VkCommandPool command_pool;
    Buffer<VkCommandBuffer> command_buffers;

    Buffer<VkFence> in_flight_fences;
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

    U32 max_texture_count;
    U32 texture_binding;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSet texture_descriptor_set;

    // ~mgj: Asset Streaming
    AssetManager* asset_manager;

    // ~mgj: Profiling
    TracyVkCtx tracy_ctx[MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Rendering
    Arena* draw_frame_arena;
    DrawFrame* draw_frame;
    Pipeline model_3D_pipeline;
    Pipeline model_3D_instance_pipeline;
    Pipeline blend_3d_pipeline;
    BufferAllocation model_3D_instance_buffer;
};

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

// ~mgj: Vulkan Lifetime
static void
ctx_set(Context* vk_ctx);
g_internal Context*
ctx_get();

// ~mgj: Building
static void
model_3d_bucket_add(BufferAllocation* vertex_buffer_allocation,
                    BufferAllocation* index_buffer_allocation, render::Handle tex_handle,
                    B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count);
static void
model_3d_instance_bucket_add(BufferAllocation* vertex_buffer_allocation,
                             BufferAllocation* index_buffer_allocation, render::Handle tex_handle,
                             render::BufferInfo* instance_buffer_info);
static void
blend_3d_bucket_add(BufferAllocation* vertex_buffer_allocation,
                    BufferAllocation* index_buffer_allocation, render::Handle texture_handle,
                    render::Handle colormap_handle);
static Pipeline
model_3d_instance_pipeline_create(Context* vk_ctx, String8 shader_path);
static Pipeline
model_3d_pipeline_create(Context* vk_ctx, String8 shader_path);
static Pipeline
blend_3d_pipeline_create(String8 shader_path);
static void
draw_frame_reset();
static void
model_3d_rendering();
static void
model_3d_instance_rendering();
static void
blend_3d_rendering();
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
