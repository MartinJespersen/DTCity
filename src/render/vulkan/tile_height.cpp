#include "core_inc.hpp"
#include "tile_height.hpp"
#include "tile_height_compute.hpp"
#include "asset_manager.hpp"
#include "vulkan.hpp"
#include "vulkan_common.hpp"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_strings.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "os_core/os_core.hpp"
#include "render/tile_bvh.hpp"
#include "render/tile_height.hpp"
#include "shaders/shader_shared.h"
#include "misc/io.hpp"
#include "base/base_profile.hpp"
#include <vulkan/vulkan_core.h>
#include "third_party/vk_mem_alloc.h"
#include "third_party/tracy/tracy/TracyVulkan.hpp"

namespace render
{

TileHeightBatchHandle
tile_height_batch_create(U32 capacity)
{
    Assert(capacity > 0);
    vulkan::Context* context = vulkan::ctx_get();
    vulkan::AssetManager* manager = context->asset_manager;
    vulkan::TileHeightBatch* batch = {};
    for (auto* item = manager->tile_height_batch_first; item; item = item->next)
        if (item->capacity == 0)
        {
            batch = item;
            break;
        }
    if (!batch)
    {
        os_mutex_scope(manager->arena_mutex)
            batch = PushStruct(manager->arena, vulkan::TileHeightBatch);
        batch->index = ++manager->tile_height_next_index;
        batch->generation = 1;
        batch->next = manager->tile_height_batch_first;
        manager->tile_height_batch_first = batch;
    }
    Assert((U64)capacity * sizeof(shader::TileHeightGpuQuery) <= context->physical_device_properties.limits.maxStorageBufferRange);
    Assert(((U64)capacity + 63) / 64 <= context->physical_device_properties.limits.maxComputeWorkGroupCount[0]);
    batch->capacity = capacity;
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = manager->graphics_queue_family_index;
    VK_CHECK_RESULT(vkCreateCommandPool(context->device, &pool_info, nullptr, &batch->command_pool));
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = batch->command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(context->device, &command_info, &batch->command));
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK_RESULT(vkCreateFence(context->device, &fence_info, nullptr, &batch->fence));
    VmaAllocationCreateInfo query_info = {};
    query_info.usage = VMA_MEMORY_USAGE_AUTO;
    query_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    batch->queries = vulkan::_buffer_allocation_create((U64)capacity * sizeof(shader::TileHeightGpuQuery),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &query_info, nullptr);
    VmaAllocationCreateInfo result_info = {};
    result_info.usage = VMA_MEMORY_USAGE_AUTO;
    result_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    batch->results = vulkan::_buffer_allocation_create((U64)capacity * sizeof(shader::TileHeightGpuResult),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &result_info, nullptr);
    vulkan::buffer_readback_create((U64)capacity * sizeof(shader::TileHeightGpuResult), 0, &batch->readback);
    return {batch->index, batch->generation};
}

B32
tile_height_batch_submit(TileHeightBatchHandle handle, TileBvhHandle bvh,
                         Buffer<const shader::TileHeightGpuQuery> queries)
{
    prof_scope_marker; // NOLINT(bugprone-reserved-identifier)
    vulkan::TileHeightBatch* batch = {};
    B32 found = vulkan::_tile_height_batch_find(handle, &batch);
    Assert(found && queries.size <= batch->capacity);
    if (batch->in_flight || batch->retiring || queries.size == 0)
        return false;
    TileBvhResourceView view = {};
    if (!tile_bvh_resource_get(bvh, &view))
        return false;
    vulkan::Context* context = vulkan::ctx_get();
    tile_bvh_resource_retain(bvh);
    batch->bvh = bvh;
    batch->query_count = (U32)queries.size;
    batch->collected_count = 0;
    batch->in_flight = true;
    batch->empty = view.node_count == 0;
    if (batch->empty)
        return true;
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(context->asset_manager->allocator, queries.data,
        batch->queries.allocation, 0, queries.size * sizeof(shader::TileHeightGpuQuery)));
    shader::TileHeightPushConstants constants = {};
    constants.nodes = vulkan::asset_manager_buffer_item_get(view.node_buffer)->item.buffer_alloc.device_address;
    constants.entries = vulkan::asset_manager_buffer_item_get(view.entry_buffer)->item.buffer_alloc.device_address;
    constants.meshes = vulkan::asset_manager_buffer_item_get(view.mesh_address_buffer)->item.buffer_alloc.device_address;
    constants.queries = batch->queries.device_address;
    constants.results = batch->results.device_address;
    constants.node_count = view.node_count;
    constants.query_count = (U32)queries.size;
    VK_CHECK_RESULT(vkResetCommandPool(context->device, batch->command_pool, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_RESULT(vkBeginCommandBuffer(batch->command, &begin));
    {
        // End the GPU zone while this command buffer is still recording.
        TracyVkZone(context->tracy_ctx[context->current_frame], batch->command, "tile_height_sampling"); // NOLINT(bugprone-reserved-identifier)
        vulkan::_tile_height_dispatch_record(batch->command, context->tile_height_pipeline, constants,
                                            batch->results.buffer, batch->readback.buffer_alloc.buffer);
    }
    // Height batches also run when swapchain rendering skips a frame.
    TracyVkCollect(context->tracy_ctx[context->current_frame], batch->command);
    VK_CHECK_RESULT(vkEndCommandBuffer(batch->command));
    VK_CHECK_RESULT(vkResetFences(context->device, 1, &batch->fence));
    VkCommandBufferSubmitInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command_info.commandBuffer = batch->command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    VK_CHECK_RESULT(vkQueueSubmit2(context->graphics_queue, 1, &submit, batch->fence));
    return true;
}

U32
tile_height_batch_collect(TileHeightBatchHandle handle, Buffer<shader::TileHeightGpuResult> results)
{
    prof_scope_marker; // NOLINT(bugprone-reserved-identifier)
    vulkan::TileHeightBatch* batch = {};
    B32 found = vulkan::_tile_height_batch_find(handle, &batch);
    Assert(found);
    if (!batch->in_flight || !vulkan::_tile_height_batch_completed(batch))
        return 0;
    U32 count = (U32)Min(results.size, batch->query_count - batch->collected_count);
    if (count == 0)
        return 0;
    if (batch->empty)
        MemoryZero(results.data, count * sizeof(shader::TileHeightGpuResult));
    else
    {
        auto* manager = vulkan::asset_manager_get();
        VK_CHECK_RESULT(vmaInvalidateAllocation(manager->allocator, batch->readback.buffer_alloc.allocation, 0, VK_WHOLE_SIZE));
        auto* source = static_cast<const shader::TileHeightGpuResult*>(batch->readback.mapped_ptr);
        MemoryCopy(results.data, source + batch->collected_count, count * sizeof(shader::TileHeightGpuResult));
    }
    batch->collected_count += count;
    if (batch->collected_count == batch->query_count)
    {
        tile_bvh_resource_destroy(batch->bvh);
        batch->bvh = {};
        batch->in_flight = false;
    }
    return count;
}

void
tile_height_batch_destroy(TileHeightBatchHandle handle)
{
    if (!handle.gen_id)
        return;
    vulkan::TileHeightBatch* batch = {};
    B32 found = vulkan::_tile_height_batch_find(handle, &batch);
    Assert(found);
    batch->retiring = true;
    if (!batch->in_flight || vulkan::_tile_height_batch_completed(batch))
        vulkan::_tile_height_batch_release(batch);
}

} // namespace render

namespace vulkan
{

void
_tile_height_pipeline_initialize(String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Context* context = ctx_get();
    String8 path = str8_path_from_str8_list(scratch.arena, {shader_path, S("bin"), S("tile_height_comp.spv")});
    Buffer<U8> bytes = io::file_read(scratch.arena, path);
    VK_CHECK_RESULT(_tile_height_pipeline_create(context->device, {reinterpret_cast<const U32*>(bytes.data), bytes.size / sizeof(U32)},
                                                 &context->tile_height_pipeline));
}

void
_tile_height_retire_poll()
{
    auto* manager = asset_manager_get();
    for (TileHeightBatch* batch = manager->tile_height_batch_first; batch; batch = batch->next)
    {
        if (!batch->capacity)
            continue;
        B32 completed = !batch->in_flight || _tile_height_batch_completed(batch);
        if (completed && batch->bvh.gen_id)
        {
            // Uncollected CPU results no longer need the sampled mesh buffers.
            render::tile_bvh_resource_destroy(batch->bvh);
            batch->bvh = {};
        }
        if (completed && batch->retiring)
            _tile_height_batch_release(batch);
    }
}

B32
_tile_height_batch_find(render::TileHeightBatchHandle handle, TileHeightBatch** batch)
{
    for (TileHeightBatch* item = asset_manager_get()->tile_height_batch_first; item; item = item->next)
        if (item->index == handle.idx && item->generation == handle.gen_id && item->capacity)
        {
            *batch = item;
            return true;
        }
    return false;
}

B32
_tile_height_batch_completed(TileHeightBatch* batch)
{
    if (batch->empty)
        return true;
    VkResult result = vkGetFenceStatus(ctx_get()->device, batch->fence);
    if (result == VK_NOT_READY)
        return false;
    VK_CHECK_RESULT(result);
    return true;
}

void
_tile_height_batch_release(TileHeightBatch* batch)
{
    Context* context = ctx_get();
    render::tile_bvh_resource_destroy(batch->bvh);
    buffer_destroy(&batch->queries);
    buffer_destroy(&batch->results);
    buffer_readback_destroy(&batch->readback);
    vkDestroyFence(context->device, batch->fence, nullptr);
    vkDestroyCommandPool(context->device, batch->command_pool, nullptr);
    batch->bvh = {};
    batch->capacity = 0;
    batch->in_flight = false;
    batch->retiring = false;
    ++batch->generation;
    Assert(batch->generation != 0);
}

} // namespace vulkan
