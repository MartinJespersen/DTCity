#pragma once

#include "core_inc.hpp"

#include "render/render_templates.hpp"
#include "vulkan.hpp"
#include "base/base_lists_templates.hpp"

// Template implementations
namespace render
{
template <typename T>
void
mapped_buffer_add(MappedHandle<T> mut_handle, T* data)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    String8 buffer = str8((U8*)data, sizeof(T));
    String8 source = push_str8_copy(vk_ctx->render_frame_arena, buffer);
    LinkedListNode<vulkan::MappedHandleTransfer>* mut_handle_node =
        PushStruct(vk_ctx->render_frame_arena, LinkedListNode<vulkan::MappedHandleTransfer>);
    render::MappedHandle<void> handle_void = render::mapped_handle_erased(mut_handle);
    mut_handle_node->v.mapped_handle = handle_void;
    mut_handle_node->v.source = source;
    SLLQueuePush(vk_ctx->mapped_handle_list.first, vk_ctx->mapped_handle_list.last, mut_handle_node);
}

template <typename T>
MappedHandle<T>
mapped_buffer_create(Arena* arena, render::ThreadWorkerCmdCtx* thread_ctx, BufferType buffer_type, String8 debug_name)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<MappedHandleFrame<T>> handle_buffer =
        buffer_alloc<MappedHandleFrame<T>>(arena, render::MAX_FRAMES_IN_FLIGHT);

    for (U32 frame_idx = 0; frame_idx < handle_buffer.size; ++frame_idx)
    {
        MappedHandleFrame<T>* content = &handle_buffer.data[frame_idx];
        VmaAllocationCreateInfo vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_AUTO;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        vma_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        BufferInfo buffer_info = BufferInfo::empty_buffer_info<T>(arena, buffer_type);
        content->handle = vulkan::asset_manager_buffer_allocation_create(thread_ctx, &buffer_info, vma_info);
        render::AssetItem<vulkan::BufferHandle>* asset_item_buffer =
            vulkan::asset_manager_buffer_item_get(content->handle);
        vulkan::BufferHandle* buffer_handle = &asset_item_buffer->item;

#if BUILD_DEBUG
        String8 frame_debug_name = push_str8f(scratch.arena, "%.*s[%u]", str8_varg(debug_name), frame_idx);
        vulkan::asset_manager_debug_name_set(buffer_handle->buffer_alloc.allocation, frame_debug_name);
#endif

        content->data = (T*)vulkan::asset_manager_allocation_cpu_pointer_get(buffer_handle->buffer_alloc.allocation);
        AssertAlways(content->data);
    }

    MappedHandle<T> mut_handle = {};
    mut_handle.buffer = handle_buffer;
    return mut_handle;
}

template <typename T>
void
mapped_buffer_destroy(MappedHandle<T> mapped_handle)
{
    for (auto h : mapped_handle.buffer)
    {
        render::handle_destroy(h.handle);
    }
}

template <typename T>
bool
is_resource_loaded(render::Handle handle, render::AssetItem<T>** out_asset)
{
    if (out_asset)
    {
        *out_asset = 0;
    }

    if (render::is_handle_zero(handle))
    {
        return false;
    }

    T* type_marker = 0;
    render::AssetItem<T>* asset = _render_asset_item_get(handle, type_marker);
    if (out_asset)
    {
        *out_asset = asset;
    }

    return asset ? asset->is_loaded : false;
}
}
