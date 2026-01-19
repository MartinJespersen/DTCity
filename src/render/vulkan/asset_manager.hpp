#pragma once

namespace vulkan
{

typedef void (*ThreadLoadingFunc)(void* data, VkCommandBuffer cmd, render::Handle handle);
typedef void (*ThreadDoneLoadingFunc)(render::Handle handle);

struct ThreadInput
{
    Arena* arena;
    render::Handle handle;

    void* user_data;
    ThreadLoadingFunc loading_func;
    ThreadDoneLoadingFunc done_loading_func;
};

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
    U32 descriptor_set_idx;
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
    ThreadInput* thread_input;
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

    // ~mgj: VMA Allocator (moved from Context)
    VmaAllocator allocator;

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

    // ~mgj: Vulkan resources needed for asset operations
    VkDevice device;
    VkQueue graphics_queue;
    U32 graphics_queue_family_index;
};

//~mgj: Asset Manager Lifecycle

g_internal AssetManager*
asset_manager_get();
static AssetManager*
asset_manager_create(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance,
                     VkQueue graphics_queue, U32 queue_family_index, async::Threads* threads,
                     U64 total_size_in_bytes);
static void
asset_manager_destroy(AssetManager* asset_manager);

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

//~mgj: Asset Item Management
static render::AssetItem<Texture>*
asset_manager_texture_item_get(render::Handle handle);
template <typename T>
static render::AssetItem<T>*
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItem<T>** free_list,
                          U32* out_desc_idx);
template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::AssetItemList<T>* list, render::Handle handle);

//~mgj: Command Management
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

//~mgj: Asset Free Functions
static void
asset_manager_buffer_free(render::Handle handle);
static void
asset_manager_texture_free(render::Handle handle);

static void
asset_cmd_queue_item_enqueue(U32 thread_id, VkCommandBuffer cmd, ThreadInput* thread_input);

//~mgj: Texture Functions
static void
texture_destroy(Texture* texture);
g_internal void
blit_transition_image(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout src_layout,
                      VkImageLayout dst_layout, U32 mip_level);
g_internal void
texture_ktx_cmd_record(VkCommandBuffer cmd, Texture* tex, ::Buffer<U8> tex_buf);
g_internal B32
texture_cmd_record(VkCommandBuffer cmd, Texture* tex, ::Buffer<U8> tex_buf);
g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle,
                                 ::Buffer<U8> tex_buf);
g_internal void
colormap_texture_cmd_record(VkCommandBuffer cmd, Texture* tex, Buffer<U8> buf);

//~mgj: Loading Thread Functions
static void
buffer_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle);
static void
buffer_done_loading_thread(render::Handle handle);
static void
colormap_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle);
static void
texture_done_loading(render::Handle handle);
g_internal void
texture_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle);
static ThreadInput*
thread_input_create();
static void
thread_input_destroy(ThreadInput* thread_input);

static void
thread_main(async::ThreadInfo thread_info, void* input);

} // namespace vulkan
