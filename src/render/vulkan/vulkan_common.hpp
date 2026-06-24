#pragma once

namespace vulkan
{

struct Context;

struct ImageSwapchainResource
{
    VkImage image;
    VkDeviceSize size;
    ImageViewResource image_view_resource;
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    Buffer<VkSurfaceFormatKHR> formats;
    Buffer<VkPresentModeKHR> presentModes;
};

struct SwapchainResources
{
    Arena* arena;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    U32 image_count;

    ImageResource color_image_resource;
    VkFormat color_format;
    ImageResource depth_image_resource;
    VkFormat depth_format;
    Buffer<ImageSwapchainResource> image_resources;

    // object id location resource
    VkFormat object_id_image_format;
    Buffer<ImageResource> object_id_image_resources;
    Buffer<ImageResource> object_id_image_resolve_resources;
    BufferReadback object_id_buffer_readback[render::MAX_FRAMES_IN_FLIGHT];

    // sync objects
    Buffer<VkSemaphore> image_available_semaphores;
    Buffer<VkSemaphore> render_finished_semaphores;
    Buffer<VkFence> image_in_flight_fences;
};

struct QueueFamilyIndices
{
    U32 graphicsFamilyIndex;
    U32 presentFamilyIndex;
};

struct ShaderModuleInfo
{
    VkPipelineShaderStageCreateInfo info;
    VkDevice device;

    ~ShaderModuleInfo()
    {
        vkDestroyShaderModule(device, info.module, nullptr);
    }
};

struct QueueFamilyIndexBits
{
    U32 graphicsFamilyIndexBits;
    U32 presentFamilyIndexBits;
};

// ~mgj: Globals
static PFN_vkCmdSetColorWriteEnableEXT cmd_set_color_write_enable_ext = VK_NULL_HANDLE;
static PFN_vkCmdPushDescriptorSetKHR cmd_push_descriptor_set_khr = VK_NULL_HANDLE;
static PFN_vkCmdBeginDebugUtilsLabelEXT cmd_begin_debug_utils_label_ext = VK_NULL_HANDLE;
static PFN_vkCmdEndDebugUtilsLabelEXT cmd_end_debug_utils_label_ext = VK_NULL_HANDLE;

#if BUILD_DEBUG
#define CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(cmd, n) cmd_begin_debug_utils_label_ext(cmd, n)
#define CMD_END_DEBUG_UTILS_LABEL_EXT(cmd) cmd_end_debug_utils_label_ext(cmd)
#else
#define CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(cmd, n)
#define CMD_END_DEBUG_UTILS_LABEL_EXT(cmd)
#endif

// ~mgj: Images (non-VMA functions remain here)
static void
swapchain_image_resource_create(VkDevice device, SwapchainResources* swapchain_resources, U32 image_count);
static ImageViewResource
image_view_resource_create(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, U32 mipmap_level, VkImageViewType image_type = VK_IMAGE_VIEW_TYPE_2D);
static void
image_view_resource_destroy(ImageViewResource image_view_resource);

// ~mgj: Swapchain functions
static U32
swapchain_image_count_get(VkDevice device, SwapchainResources* swapchain_resources);
static SwapchainResources*
swapchain_create(Context* vk_ctx, SwapChainSupportDetails* swapchain_info, VkExtent2D swapchain_extent);

static ShaderModuleInfo
shader_stage_from_spirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path);
static VkShaderModule
shader_module_create(VkDevice device, Buffer<U8> buffer);

static QueueFamilyIndices
queue_family_indices_from_bit_fields(QueueFamilyIndexBits queueFamilyBits);

static bool
queue_family_is_complete(QueueFamilyIndexBits queueFamily);

static QueueFamilyIndexBits
queue_families_find(Context* vk_ctx, VkPhysicalDevice device);

static bool
is_device_suitable(Context* vk_ctx, VkPhysicalDevice device, QueueFamilyIndexBits indexBits);

static SwapChainSupportDetails
query_swapchain_support(Arena* arena, VkPhysicalDevice device, VkSurfaceKHR surface);

static render::BufferType
buffer_type_from_usage_flags(VkBufferUsageFlags usage);

static void
color_resources_create(Context* vk_ctx, SwapchainResources* swapchain_resources);

// sampler helpers
static VkSampler
sampler_create(VkDevice device, VkSamplerCreateInfo* sampler_info);

// ~mgj: Descriptor Related Functions
static void
descriptor_pool_create(Context* vk_ctx, U32 max_textures);

// ~mgj: Descriptor Indexing / Bindless Descriptor Functions
// Creates a descriptor set layout with descriptor indexing flags for bindless resources
static VkDescriptorSetLayout
descriptor_set_layout_create_bindless(VkDevice device, VkDescriptorSetLayoutBinding* bindings, VkDescriptorBindingFlags* binding_flags, U32 binding_count);

// Creates a descriptor set layout for bindless textures
static VkDescriptorSetLayout
descriptor_set_layout_create_bindless_textures(VkDevice device, U32 texture_binding, U32 max_textures, VkShaderStageFlags stage_flags);

// Allocates a descriptor set with variable descriptor count for bindless arrays
static VkDescriptorSet
descriptor_set_allocate_bindless(VkDevice device, VkDescriptorPool desc_pool, VkDescriptorSetLayout desc_set_layout, U32 variable_count);

// Updates a single texture in a bindless descriptor set at the specified array index
static void
descriptor_set_update_bindless_texture(U32 array_index, VkImageView image_view, VkSampler sampler);

// Clears a bindless descriptor slot by writing the null texture
static void
descriptor_set_clear_bindless_texture(U32 array_index, render::AssetItem<TextureHandle>* null_texture);

// image helpers

g_internal void
blit_transition_image(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout src_layout, VkImageLayout dst_layout, U32 mip_level);
// queue family

static void
depth_resources_create(Context* vk_context, SwapchainResources* swapchain_resources);

static void
swapchain_recreate(Vec2U32 framebuffer_dim);

static void
sync_objects_create(Context* vk_ctx);

static VkCommandPool
command_pool_create(VkDevice device, VkCommandPoolCreateInfo* poolInfo);

static void
color_resources_cleanup(ImageResource color_image_resource);
static void
swapchain_cleanup(VkDevice device, SwapchainResources* swapchain_resources);
static void
create_instance(Context* vk_ctx);
static void
debug_messenger_setup(Context* vk_ctx);
static void
surface_create(Context* vk_ctx, io::IO* io_ctx);
static void
physical_device_pick(Context* vk_ctx);
static void
logical_device_create(Arena* arena, Context* vk_ctx);

static VkExtent2D
choose_swap_extent(Vec2U32 framebuffer_dim, const VkSurfaceCapabilitiesKHR& capabilities);

static Buffer<String8>
required_extensions_get(Context* vk_ctx);

static void
populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

static VkSampleCountFlagBits
max_usable_sample_count_get(VkPhysicalDevice device);

static bool
check_device_extension_support(Context* vk_ctx, VkPhysicalDevice device);

static bool
check_validation_layer_support(Context* vk_ctx);

static VkSurfaceFormatKHR
choose_swap_surface_format(Buffer<VkSurfaceFormatKHR> availableFormats);

static VkFormat
supported_format(VkPhysicalDevice physical_device, VkFormat* candidates, U32 candidate_count, VkImageTiling tiling, VkFormatFeatureFlags features);
static VkPresentModeKHR
choose_swap_present_mode(Buffer<VkPresentModeKHR> availablePresentModes);

static void
command_buffers_create(Context* vk_ctx);

static VkResult
create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);

static void
destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

// ~mgj: sampler functions
static void
sampler_create_info_from_sampler_info(render::SamplerInfo* sampler, VkSamplerCreateInfo* out_sampler_info);

} // namespace vulkan
