#pragma once

namespace vulkan
{

struct BufferAllocation
{
    VkBuffer buffer;
    VmaAllocation allocation;
    U32 size;
};

struct BufferReadback
{
    BufferAllocation buffer_alloc;
    void* mapped_ptr;
};

struct ImageAllocation
{
    VkImage image;
    VmaAllocation allocation;
    VkDeviceSize size;
    VkExtent3D extent;
};

struct ImageViewResource
{
    VkImageView image_view;
    VkDevice device;

    ImageViewResource(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, U32 mipmap_level, VkImageViewType image_type);
};

struct ImageResource
{
    ImageAllocation image_alloc;
    ImageViewResource image_view_resource;

    ImageResource(ImageAllocation image_alloc, ImageViewResource image_view_resource) : image_alloc(image_alloc), image_view_resource(image_view_resource)
    {
    }
};

struct ImageAllocationResource
{
    ImageResource image_resource;
    BufferAllocation staging_buffer_alloc;

    ImageAllocationResource(ImageResource image_resource, BufferAllocation staging_buffer_alloc) : image_resource(image_resource), staging_buffer_alloc(staging_buffer_alloc)
    {
    }
};

struct BufferHandle
{
    render::BufferType type;
    BufferAllocation buffer_alloc;
    VkMemoryPropertyFlags mem_prop_flags;
    BufferAllocation staging_buffer;
    U32 item_byte_size;
    U32 elem_count;
};

struct TextureHandle
{
    BufferAllocation staging_allocation;
    ImageResource image_resource;
    VkSampler sampler;
    U32 descriptor_set_idx;
};

struct DescriptorSetInfo
{
    VkDescriptorSet set;
    VkDescriptorSetLayout layout;

    DescriptorSetInfo(VkDescriptorSet set, VkDescriptorSetLayout layout) : set(set), layout(layout)
    {
    }
};

struct AssetManagerCommandPool
{
    OS_Handle mutex;
    VkCommandPool cmd_pool;
};

struct CmdQueueItem
{
    CmdQueueItem* next;
    CmdQueueItem* prev;
    render::ThreadWorkerCmdCtx* thread_input;
    U32 thread_id;
    VkFence fence;
};

struct AssetManagerCmdList
{
    Arena* arena;
    CmdQueueItem* list_first;
    CmdQueueItem* list_last;

    CmdQueueItem* free_list;
};

struct AssetManagerCmdQueue
{
    Arena* arena;
    OS_Handle mutex;
    CmdQueueItem* first;
    CmdQueueItem* last;
    CmdQueueItem* free_list;
    U32 count;
};

template <typename T>
struct AssetList
{
    Arena* arena;
    render::AssetItemList<T> list;
    render::AssetItem<T>* free_list;
};

// ~mgj: Stable index allocator for bindless descriptor array slots.
// Uses a free list of returned indices, falling back to a monotonic counter.
struct DescriptorIndexAllocator
{
    U32* free_indices;
    U32 free_count;
    U32 free_capacity;
    U32 next_index;
    U32 max_index;
};

struct PendingDeletion
{
    PendingDeletion* next;
    PendingDeletion* prev;
    render::Handle handle;
};

struct DeletionQueue
{
    PendingDeletion* first;
    PendingDeletion* last;
    U32 list_count;
};

struct AssetManager
{
    Arena* arena;

    // ~mgj: VMA Allocator (moved from Context)
    VmaAllocator allocator;

    // ~mgj: Textures
    OS_Handle texture_mutex;
    render::AssetItemList<TextureHandle> texture_list;
    render::AssetItemList<TextureHandle> texture_free_list;

    // ~mgj: Buffers
    OS_Handle buffer_mutex;
    render::AssetItemList<BufferHandle> buffer_list;
    render::AssetItemList<BufferHandle> buffer_free_list;

    // ~mgj: Threading Buffer Commands
    Buffer<AssetManagerCommandPool> threaded_cmd_pools;
    VkCommandPool main_thread_cmd_pool;
    U64 total_size;
    AssetManagerCmdQueue* cmd_queue;
    async::ThreadPool* threads;
    AssetManagerCmdList* cmd_wait_list;

    // ~mgj: Bindless descriptor index allocator
    VkDescriptorPool descriptor_pool;
    DescriptorIndexAllocator descriptor_index_allocator;

    // ~mgj: Deferred Deletion Queue
    DeletionQueue deletion_queues[render::MAX_FRAMES_IN_FLIGHT + 1];
    U64 deletion_queue_idx;
    PendingDeletion* deletion_queue_free_list;
    U32 deletion_queue_free_list_count;
    B32 shutting_down;

    // ~mgj: Arena mutex - protects concurrent PushStruct calls across different asset types
    OS_Handle arena_mutex;

    // ~mgj: Vulkan resources needed for asset operations
    VkDevice device;
    VkQueue graphics_queue;
    U32 graphics_queue_family_index;
};

//~mgj: Asset Manager Lifecycle

g_internal AssetManager*
asset_manager_get();
static AssetManager*
asset_manager_create(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance, VkQueue graphics_queue, U32 queue_family_index, async::ThreadPool* threads, U64 total_size_in_bytes,
                     VkDescriptorPool desc_pool);
static void
asset_manager_destroy(AssetManager* asset_manager);

//~mgj: Buffer Allocation Functions (VMA)
// buffer usage patterns with VMA:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static BufferAllocation
_buffer_allocation_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateInfo vma_info, const char* name);

static void
buffer_destroy(BufferAllocation* buffer_allocation);
static BufferAllocation
staging_buffer_create(VkDeviceSize size);
static BufferAllocation
staging_buffer_mapped_create(VkDeviceSize size, const char* name);
static render::Handle
buffer_alloc_create_or_resize(U32 total_buffer_byte_count, render::Handle handle, VkBufferUsageFlags usage, const char* name);
static void
buffer_readback_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage, BufferReadback* out_buffer_readback, const char* name);
static void
buffer_readback_destroy(BufferReadback* out_buffer_readback);

//~mgj: Image Allocation Functions (VMA)
static ImageAllocation
image_allocation_create(U32 width, U32 height, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, U32 mipmap_level, VmaAllocationCreateInfo vma_info,
                        const char* name, VkImageType image_type = VK_IMAGE_TYPE_2D);
static void
image_allocation_destroy(ImageAllocation image_alloc);
static void
image_resource_destroy(ImageResource image);

//~mgj: Descriptor Index Allocator
static void
descriptor_index_allocator_init(DescriptorIndexAllocator* alloc, Arena* arena, U32 max_index);
static U32
descriptor_index_allocate(DescriptorIndexAllocator* alloc);
static void
descriptor_index_free(DescriptorIndexAllocator* alloc, U32 index);

//~mgj: Deferred Deletion Queue
static void
deletion_queue_push(render::Handle handle);
static void
deletion_queue_empty(DeletionQueue* queue);
g_internal void
deletion_queue_empty_next();
static void
deletion_queue_empty_all();

//~mgj: Asset Item Management

static render::AssetItem<BufferHandle>*
asset_manager_buffer_item_get(render::Handle handle);
static render::AssetItem<TextureHandle>*
asset_manager_texture_item_get(render::Handle handle);

template <typename T>
static render::Handle
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItemList<T>* free_list, render::HandleType handle_type);
template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::Handle handle);

static void
asset_manager_handle_free(render::Handle handle);
g_internal ImageAllocationResource
texture_upload_with_blitting(VkCommandBuffer cmd, render::TextureUploadData* data);
//~mgj: Command Management
static void
asset_manager_execute_cmds();
static void
asset_manager_cmd_done_check();
static AssetManagerCommandPool
asset_manager_cmd_pool_get(AssetManager* asset_manager, U32 thread_id);
static VkCommandBuffer
begin_command(VkDevice device, AssetManagerCommandPool* threaded_cmd_pool);
static void
end_command(AssetManagerCommandPool* threaded_cmd_pool, VkCommandBuffer command_buffer);
static AssetManagerCmdQueue*
asset_manager_cmd_queue_create();
static void
asset_manager_cmd_queue_destroy(AssetManagerCmdQueue* cmd_queue);
static void
asset_manager_cmd_queue_enqueue(AssetManagerCmdQueue* cmd_queue, CmdQueueItem item);
static B32
asset_manager_cmd_queue_try_dequeue(AssetManagerCmdQueue* cmd_queue, CmdQueueItem* item);
static B32
asset_manager_cmd_queue_has_pending_work(AssetManagerCmdQueue* cmd_queue);
static AssetManagerCmdList*
asset_manager_cmd_list_create();
static void
asset_manager_cmd_list_destroy(AssetManagerCmdList* cmd_wait_list);
static void
asset_manager_cmd_list_add(AssetManagerCmdList* cmd_list, CmdQueueItem item);
static void
asset_manager_cmd_list_item_remove(AssetManagerCmdList* cmd_list, CmdQueueItem* item);

//~mgj: Asset Free Functions

static void
asset_manager_buffer_free(render::Handle handle);
static void
asset_manager_texture_free(render::Handle handle);
g_internal render::Handle
asset_manager_buffer_allocation_create(render::ThreadWorkerCmdCtx* thread_ctx, render::BufferInfo* buffer_info, VmaAllocationCreateInfo vma_info);
g_internal vulkan::BufferAllocation
asset_manager_buffer_from_staging(VkCommandBuffer cmd_buffer, render::BufferInfo* buffer_info, VkBuffer dest_buffer);
static void
asset_cmd_queue_item_enqueue(U32 thread_id, render::ThreadWorkerCmdCtx* thread_input);

//~mgj: Texture Functions
static void
texture_destroy(TextureHandle* texture);
g_internal void
texture_ktx_cmd_record(VkCommandBuffer cmd, TextureHandle* tex, ::Buffer<U8> tex_buf);
g_internal B32
texture_cmd_record_with_stb(VkCommandBuffer cmd, TextureHandle* tex, ::Buffer<U8> tex_buf);
g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle, ::Buffer<U8> tex_buf);
g_internal void
colormap_texture_cmd_record(VkCommandBuffer cmd, TextureHandle* tex, Buffer<U8> buf);

//~mgj: Loading Thread Functions
static void
buffer_loading_thread(void* data, render::ThreadWorkerCmdCtx* thread_input);
static void
colormap_loading_thread(void* data, render::ThreadWorkerCmdCtx* thread_input);
g_internal void
colormap_loading_thread(render::Handle handle, render::ColorMapLoadingInfo* colormap_info, render::ThreadWorkerCmdCtx* thread_input);

g_internal void
texture_loading_from_path_thread(void* data, render::ThreadWorkerCmdCtx* thread_input);

static async::WorkerResult
thread_main(async::ThreadInfo thread_info, async::WorkerData input);

} // namespace vulkan

#define VK_CHECK_RESULT(f)                                                                                                                                                                             \
    {                                                                                                                                                                                                  \
        VkResult res = (f);                                                                                                                                                                            \
        if (res != VK_SUCCESS)                                                                                                                                                                         \
        {                                                                                                                                                                                              \
            ERROR_LOG("Fatal : VkResult is %d in %s:%d\n", res, __FILE__, __LINE__);                                                                                                                   \
            Trap();                                                                                                                                                                                    \
            exit(EXIT_FAILURE);                                                                                                                                                                        \
        }                                                                                                                                                                                              \
    }
