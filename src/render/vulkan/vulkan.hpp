#pragma once

namespace vulkan
{

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
};

struct Model3dPushConstants
{
    U32 tex_idx;
    U32 colormap_idx;
    U32 overlay_tex_idx;
    U32 overlay_enabled;
    F32 overlay_translation_x;
    F32 overlay_translation_y;
    F32 overlay_scale_x;
    F32 overlay_scale_y;
    F32 bbox_min_x;
    F32 bbox_min_y;
    F32 bbox_max_x;
    F32 bbox_max_y;
    F32 height_offset;
};

struct Model3DNode
{
    Model3DNode* next;
    B32 depth_write_per_draw_enabled;
    BufferAllocation index_alloc;
    U32 index_buffer_offset;
    U32 index_count;
    BufferAllocation vertex_alloc;
    F32 depth_bias;
    Model3dPushConstants push_constants;
};

struct CarInstancePushConstants
{
    U32 tex_idx;
};

struct CarHeightCalculatePushConstants
{
    U32 car_count;
    F32 car_center_offset;
};

struct CarInstanceComputeNode
{
    CarInstanceComputeNode* next;

    // Compute pipeline ressources
    BufferHandle* tile_index_handle;
    BufferHandle* tile_vertex_handle;
    CarHeightCalculatePushConstants compute_push_constants;

    // shared pipeline ressources
    render::BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
};

struct CarInstanceRenderNode
{
    CarInstanceRenderNode* next;

    // draw pipeline ressources
    BufferHandle* index_handle;
    BufferHandle* vertex_handle;
    CarInstancePushConstants draw_push_constants;

    // shared pipeline ressources
    render::BufferInfo instance_buffer_info;
    U32 instance_buffer_offset;
};

struct Model3DNodeList
{
    Model3DNode* first;
    Model3DNode* last;
};

struct CarInstanceComputeNodeList
{
    CarInstanceComputeNode* first;
    CarInstanceComputeNode* last;
};

struct CarInstanceRenderNodeList
{
    CarInstanceRenderNode* first;
    CarInstanceRenderNode* last;
};

struct CarInstanceCompute
{
    CarInstanceComputeNodeList list;
};

struct CarInstanceRender
{
    CarInstanceRenderNodeList list;
    U32 total_instance_buffer_byte_count;
};

struct Blend3dPushConstants
{
    U32 texture_index;
    U32 colormap_index;
};

struct RoadIntersectionPushConstants
{
    U32 road_segment_buffer_elem_count;
    U32 overlay_option_idx;
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

struct RoadIntersectionNode
{
    RoadIntersectionNode* next;
    BufferHandle vertex_buffer;
    BufferHandle index_buffer;
    BufferHandle road_segment_buffer;
    BufferHandle road_segment_node_buffer;
    U32 overlay_option_idx;
};

struct RoadIntersectionList
{
    RoadIntersectionNode* first;
    RoadIntersectionNode* last;
};

struct RenderFrame
{
    Model3DNodeList model_3D_list;
    CarInstanceCompute car_instance_compute_list;
    CarInstanceRender car_instance_render_list;
    Blend3DList blend_3d_list;
    RoadIntersectionList road_intersection_list;
};

struct Context
{
    static const U32 WIDTH = 800;
    static const U32 HEIGHT = 600;

#if BUILD_DEBUG
    static const U8 enable_validation_layers = 1;
    static const U8 enable_gpu_assisted_validation = 0;
#else
    static const U8 enable_validation_layers = 0;
    static const U8 enable_gpu_assisted_validation = 0;
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
    U64 current_frame;

    VkFormat object_id_format;
    U64 hovered_object_id;
    SwapchainResources* swapchain_resources;

    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    // Graphics and Compute Queue
    QueueFamilyIndices queue_family_indices;
    VkDescriptorPool descriptor_pool;

    // ~mgj: camera resources for uniform buffers
    BufferAllocationMapped camera_buffer_alloc_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout camera_descriptor_set_layout;
    VkDescriptorSet camera_descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    U32 max_texture_count;
    U32 texture_binding;
    VkDescriptorSetLayout bindless_descriptor_set_layout;
    VkDescriptorSet bindless_descriptor_set;

    // ~mgj: Null texture used to clear freed bindless descriptor slots
    render::Handle null_texture_handle;

    // ~mgj: Asset Streaming
    AssetManager* asset_manager;

    // ~mgj: Profiling
    TracyVkCtx tracy_ctx[MAX_FRAMES_IN_FLIGHT];

    // ~mgj: Rendering
    Arena* render_frame_arena;
    RenderFrame* render_frame;
    Pipeline model_3D_pipeline;
    Pipeline car_instance_pipeline;
    Pipeline blend_3d_pipeline;
    Pipeline road_intersection_pipeline;
    Pipeline car_height_calculate_pipeline;
    Pipeline bbox_pipeline;
    VkDescriptorSetLayout road_segment_descriptor_set_layout;
    VkDescriptorSetLayout storage_buffer_descriptor_set_layout;
    VkDescriptorSetLayout car_height_calculate_descriptor_set_layout;
    render::Handle model_3D_instance_buffer[MAX_FRAMES_IN_FLIGHT];
};

//~mgj: camera functions
static void
camera_cleanup(Context* vk_ctx);

static void
camera_uniform_buffer_create(Context* vk_ctx);
static void
camera_uniform_buffer_update(Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res, U32 current_frame);
static void
camera_descriptor_set_layout_create(Context* vk_ctx);
static void
camera_descriptor_set_create(Context* vk_ctx);

// ~mgj: Vulkan Lifetime
static void
ctx_set(Context* vk_ctx);
static void
ctx_release();
g_internal Context*
ctx_get();

// ~mgj: Descriptor Sets Functions

struct UniformBufferDescriptor
{
    VkBuffer uniform_buffer;
};

g_internal DescriptorSetInfo
descriptor_set_uniform_buffer(VkDevice device, VkDescriptorPool desc_pool, void* data);

struct StorageBufferDescriptor
{
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
};

g_internal DescriptorSetInfo
descriptor_set_storage_buffers(VkDevice device, VkDescriptorPool desc_pool, void* data);

struct RoadSegmentDescriptor
{
    VkBuffer road_segment_buffer;
    VkBuffer road_segment_node_buffer;
};

g_internal DescriptorSetInfo
descriptor_set_road_segment(VkDevice device, VkDescriptorPool desc_pool, void* data);

// ~mgj: Building
static void
model_3d_bucket_add(render::Model3DPipelineData* pipeline_input);
static void
blend_3d_bucket_add(BufferAllocation* vertex_buffer_allocation, BufferAllocation* index_buffer_allocation, render::Handle texture_handle, render::Handle colormap_handle);

g_internal void
road_intersection_compute();
g_internal void
car_instance_compute();
static void
road_intersection_bucket_add(BufferHandle* vertex_buffer, BufferHandle* index_buffer, BufferHandle* road_segment_buffer, BufferHandle* road_segment_node_buffer, U32 overlay_option);

static void
model_3d_rendering();
static void
car_instance_rendering();
static void
blend_3d_rendering();
static void
pipeline_destroy(Pipeline* draw_ctx);

static void
command_buffer_record(U32 image_index, U32 current_frame, ui::Camera* camera, Vec2S64 mouse_cursor_pos);

static void
profile_buffers_create(Context* vk_ctx);
static void
profile_buffers_destroy(Context* vk_ctx);

} // namespace vulkan
