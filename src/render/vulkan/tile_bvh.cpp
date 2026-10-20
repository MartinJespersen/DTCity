#include "core_inc.hpp"
#include "tile_bvh.hpp"
#include "asset_manager.hpp"
#include "async/thread_pool.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_profile.hpp"
#include "base/base_math.h"
#include "base/base_strings.hpp"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include "shaders/shader_shared.h"
#include "os_core/os_core_inc.hpp"
#include "os_core/os_core.hpp"
#include <new>

namespace render
{

TileBvhHandle
tile_bvh_resource_create(Buffer<const Rng3F32> bounds, Buffer<const TileBvhGpuEntry> entries,
                         Buffer<const TileBvhMeshBuffers> meshes)
{
    Assert(bounds.size == entries.size && entries.size == meshes.size);
    Assert(meshes.size <= max_U32);
    vulkan::AssetManager* manager = vulkan::asset_manager_get();
    vulkan::TileBvhResource* resource = manager->tile_bvh_resource_free;
    if (resource)
    {
        manager->tile_bvh_resource_free = resource->next_free;
        resource->next_free = 0;
    }
    else
    {
        os_mutex_scope(manager->arena_mutex)
        {
            resource = new (PushStructNoZero(manager->arena, vulkan::TileBvhResource)) vulkan::TileBvhResource{};
        }
        resource->index = ++manager->tile_bvh_next_index;
        resource->generation = 1;
        resource->next = manager->tile_bvh_resource_first;
        manager->tile_bvh_resource_first = resource;
    }
    resource->reference_count = 1;
    resource->view = {};
    resource->view.entry_count = (U32)entries.size;
    if (bounds.size)
    {
        resource->view.city_bounds = bounds.data[0];
        for (U64 index = 1; index < bounds.size; ++index)
            for (U32 axis = 0; axis < 3; ++axis)
            {
                resource->view.city_bounds.min.v[axis] = Min(resource->view.city_bounds.min.v[axis], bounds.data[index].min.v[axis]);
                resource->view.city_bounds.max.v[axis] = Max(resource->view.city_bounds.max.v[axis], bounds.data[index].max.v[axis]);
            }
    }
    resource->meshes = Array<TileBvhMeshBuffers>((U32)meshes.size);
    for (U32 index = 0; index < meshes.size; ++index)
    {
        const TileBvhMeshBuffers& mesh = meshes.data[index];
        Assert(!is_handle_zero(mesh.mesh.vertex_buffer_handle));
        Assert(!is_handle_zero(mesh.mesh.index_buffer_handle));
        Assert(!is_handle_zero(mesh.triangle_bvh));
        buffer_retain(mesh.mesh.vertex_buffer_handle);
        buffer_retain(mesh.mesh.index_buffer_handle);
        buffer_retain(mesh.triangle_bvh);
        resource->meshes.data[index] = mesh;
    }
    TileBvhHandle handle = {resource->index, resource->generation};
    resource->ready = meshes.size == 0;
    if (resource->ready)
        return handle;

    // Snapshot inputs into the existing upload context lifetime. Workers never
    // access TileBvh, a tile batch, or its owning city.
    ThreadWorkerCmdCtx* thread_ctx = thread_ctx_create();
    vulkan::TileBvhUpload* upload = PushStruct(thread_ctx->arena, vulkan::TileBvhUpload);
    upload->resource = resource;
    upload->bounds = buffer_alloc<Rng3F32>(thread_ctx->arena, bounds.size);
    upload->entries = buffer_alloc<TileBvhGpuEntry>(thread_ctx->arena, entries.size);
    MemoryCopy(upload->bounds.data, bounds.data, sizeof(Rng3F32) * bounds.size);
    MemoryCopy(upload->entries.data, entries.data, sizeof(TileBvhGpuEntry) * entries.size);
    thread_ctx->user_data = upload;
    thread_ctx->gpu_work_done_func = vulkan::_tile_bvh_upload_complete;
    ++resource->reference_count; // Upload owns a reference until its completion callback.
    async::WorkerItem work(thread_ctx, vulkan::_tile_bvh_build_worker);
    if (!async::thread_pool_push(thread_ctx->thread_pool, &work))
    {
        thread_input_destroy(thread_ctx);
        tile_bvh_resource_destroy(handle);
        tile_bvh_resource_destroy(handle);
        return {};
    }
    return handle;
}

B32
tile_bvh_resource_get(TileBvhHandle handle, TileBvhResourceView* view)
{
    vulkan::TileBvhResource* resource = {};
    if (!vulkan::_tile_bvh_resource_find(handle, &resource) || !resource->ready)
        return false;
    *view = resource->view;
    return true;
}

void
tile_bvh_resource_retain(TileBvhHandle handle)
{
    vulkan::TileBvhResource* resource = {};
    B32 found = vulkan::_tile_bvh_resource_find(handle, &resource);
    Assert(found);
    ++resource->reference_count;
}

void
tile_bvh_resource_destroy(TileBvhHandle handle)
{
    if (!handle.gen_id)
        return;
    vulkan::TileBvhResource* resource = {};
    B32 found = vulkan::_tile_bvh_resource_find(handle, &resource);
    Assert(found);
    if (--resource->reference_count)
        return;
    handle_destroy_deferred(resource->view.node_buffer);
    handle_destroy_deferred(resource->view.entry_buffer);
    handle_destroy_deferred(resource->view.mesh_address_buffer);
    for (const TileBvhMeshBuffers& mesh : resource->meshes)
    {
        handle_destroy_deferred(mesh.mesh.vertex_buffer_handle);
        handle_destroy_deferred(mesh.mesh.index_buffer_handle);
        handle_destroy_deferred(mesh.triangle_bvh);
    }
    resource->meshes = {};
    resource->view = {};
    resource->ready = false;
    ++resource->generation;
    Assert(resource->generation != 0);
    vulkan::AssetManager* manager = vulkan::asset_manager_get();
    resource->next_free = manager->tile_bvh_resource_free;
    manager->tile_bvh_resource_free = resource;
}

} // namespace render

namespace vulkan
{

// Owner-thread registry lookup. Resource storage remains stable while workers run.
B32
_tile_bvh_resource_find(render::TileBvhHandle handle, TileBvhResource** resource)
{
    AssetManager* manager = asset_manager_get();
    for (TileBvhResource* item = manager->tile_bvh_resource_first; item; item = item->next)
    {
        if (item->index == handle.idx && item->generation == handle.gen_id && item->reference_count)
        {
            *resource = item;
            return true;
        }
    }
    return false;
}

async::WorkerResult
_tile_bvh_build_worker(async::ThreadInfo info, async::WorkerData data)
{
    prof_scope_marker; // NOLINT(bugprone-reserved-identifier)
    (void)info;
    auto* thread_ctx = static_cast<render::ThreadWorkerCmdCtx*>(data);
    auto* upload = static_cast<TileBvhUpload*>(thread_ctx->user_data);
    render::TileBvhBuild tree = render::tile_bvh_nodes_build(
        thread_ctx->arena, {upload->bounds.data, upload->bounds.size}, 4);
    Buffer<render::TileBvhGpuEntry> ordered_entries =
        buffer_alloc<render::TileBvhGpuEntry>(thread_ctx->arena, upload->entries.size);
    for (U32 index = 0; index < ordered_entries.size; ++index)
        ordered_entries.data[index] = upload->entries.data[tree.item_indices.data[index]];
    // Entry indices address the original mesh order, independently of BVH sorting.
    Buffer<shader::TileBvhGpuMeshAddresses> addresses =
        buffer_alloc<shader::TileBvhGpuMeshAddresses>(thread_ctx->arena, upload->entries.size);
    for (U32 index = 0; index < addresses.size; ++index)
    {
        const render::TileBvhMeshBuffers& mesh = upload->resource->meshes.data[index];
        addresses.data[index] = {
            asset_manager_buffer_item_get(mesh.mesh.vertex_buffer_handle)->item.buffer_alloc.device_address,
            asset_manager_buffer_item_get(mesh.mesh.index_buffer_handle)->item.buffer_alloc.device_address,
            asset_manager_buffer_item_get(mesh.triangle_bvh)->item.buffer_alloc.device_address, 0};
    }

    render::thread_cmd_buffer_record(thread_ctx);
    render::BufferInfo node_info(tree.nodes, render::BufferType_StorageBuffer);
    render::BufferInfo entry_info(ordered_entries, render::BufferType_StorageBuffer);
    render::BufferInfo address_info(addresses, render::BufferType_StorageBuffer);
    upload->resource->view.node_buffer =
        render::buffer_load_sync(thread_ctx, &node_info, S("Tile BVH Nodes"));
    upload->resource->view.entry_buffer =
        render::buffer_load_sync(thread_ctx, &entry_info, S("Tile BVH Mesh Entries"));
    upload->resource->view.mesh_address_buffer =
        render::buffer_load_sync(thread_ctx, &address_info, S("Tile BVH Mesh Addresses"));
    upload->resource->view.node_count = (U32)tree.nodes.size;
    render::thread_cmd_buffer_end(thread_ctx);
    return {};
}

void
_tile_bvh_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx)
{
    auto* upload = static_cast<TileBvhUpload*>(thread_ctx->user_data);
    TileBvhResource* resource = upload->resource;
    resource->ready = true;
    render::tile_bvh_resource_destroy({resource->index, resource->generation});
}

} // namespace vulkan
