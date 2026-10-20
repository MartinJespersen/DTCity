#include "tile_bvh_test_backend.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include "render/tile_height.hpp"

namespace tile_bvh_test
{
Backend backend = {};
HeightBatch height_batches[32] = {};
U32 height_batch_count = 0;

void
tile_height_test_complete(U32 index)
{
    HeightBatch& batch = height_batches[index];
    batch.ready = true;
    if (batch.retiring)
    {
        render::tile_bvh_resource_destroy(batch.bvh);
        batch.bvh = {};
        batch.live = false;
        batch.queries = {};
        batch.results = {};
    }
}
}

namespace render
{

TileHeightBatchHandle
tile_height_batch_create(U32 capacity)
{
    U32 index = ++tile_bvh_test::height_batch_count;
    Assert(index < ArrayCount(tile_bvh_test::height_batches));
    auto& batch = tile_bvh_test::height_batches[index];
    batch.queries = Array<shader::TileHeightGpuQuery>(capacity);
    batch.results = Array<shader::TileHeightGpuResult>(capacity);
    batch.live = true;
    return {index, 1};
}

B32
tile_height_batch_submit(TileHeightBatchHandle handle, TileBvhHandle bvh,
                         Buffer<const shader::TileHeightGpuQuery> queries)
{
    auto& batch = tile_bvh_test::height_batches[handle.idx];
    Assert(batch.live && batch.count == batch.collected);
    TileBvhResourceView view = {};
    if (!tile_bvh_resource_get(bvh, &view))
        return false;
    batch.bvh = bvh;
    tile_bvh_resource_retain(bvh);
    batch.count = (U32)queries.size;
    batch.collected = 0;
    batch.ready = false;
    for (U32 index = 0; index < queries.size; ++index)
        batch.queries.data[index] = queries.data[index];
    return true;
}

U32
tile_height_batch_collect(TileHeightBatchHandle handle, Buffer<shader::TileHeightGpuResult> results)
{
    auto& batch = tile_bvh_test::height_batches[handle.idx];
    if (!batch.ready)
        return 0;
    U32 count = (U32)Min(results.size, batch.count - batch.collected);
    for (U32 index = 0; index < count; ++index)
        results.data[index] = batch.results.data[batch.collected + index];
    batch.collected += count;
    if (batch.collected == batch.count && batch.bvh.gen_id)
    {
        tile_bvh_resource_destroy(batch.bvh);
        batch.bvh = {};
    }
    return count;
}

void
tile_height_batch_destroy(TileHeightBatchHandle handle)
{
    if (!handle.gen_id)
        return;
    auto& batch = tile_bvh_test::height_batches[handle.idx];
    batch.retiring = true;
    if (batch.count == batch.collected || batch.ready)
        tile_bvh_test::tile_height_test_complete(handle.idx);
}

void
buffer_retain(Handle handle)
{
    if (!handle.u64)
        return;
    Assert(handle.u64 < ArrayCount(tile_bvh_test::backend.buffer_references));
    ++tile_bvh_test::backend.buffer_references[handle.u64];
}

void
handle_destroy_deferred(Handle handle)
{
    if (!handle.u64)
        return;
    U32& references = tile_bvh_test::backend.buffer_references[handle.u64];
    Assert(references > 0);
    --references;
}

TileBvhHandle
tile_bvh_resource_create(Buffer<const Rng3F32> bounds, Buffer<const TileBvhGpuEntry> entries,
                         Buffer<const TileBvhMeshBuffers> meshes)
{
    Assert(bounds.size == entries.size && meshes.size == entries.size);
    auto& backend = tile_bvh_test::backend;
    if (backend.reject_submission)
        return {};
    U32 index = ++backend.created;
    Assert(index < ArrayCount(backend.resources));
    auto& resource = backend.resources[index];
    resource.references = 1;
    resource.ready = meshes.size == 0;
    resource.meshes = Array<TileBvhMeshBuffers>((U32)meshes.size);
    resource.entries = Array<TileBvhGpuEntry>((U32)entries.size);
    resource.view.entry_count = (U32)entries.size;
    for (U32 item = 0; item < meshes.size; ++item)
    {
        resource.meshes.data[item] = meshes.data[item];
        resource.entries.data[item] = entries.data[item];
        buffer_retain(meshes.data[item].mesh.vertex_buffer_handle);
        buffer_retain(meshes.data[item].mesh.index_buffer_handle);
        buffer_retain(meshes.data[item].triangle_bvh);
    }
    return {index, 1};
}

B32
tile_bvh_resource_get(TileBvhHandle handle, TileBvhResourceView* view)
{
    if (!handle.gen_id)
        return false;
    auto& resource = tile_bvh_test::backend.resources[handle.idx];
    if (!resource.references || !resource.ready)
        return false;
    *view = resource.view;
    return true;
}

void
tile_bvh_resource_retain(TileBvhHandle handle)
{
    auto& resource = tile_bvh_test::backend.resources[handle.idx];
    Assert(resource.references > 0);
    ++resource.references;
}

void
tile_bvh_resource_destroy(TileBvhHandle handle)
{
    if (!handle.gen_id)
        return;
    auto& resource = tile_bvh_test::backend.resources[handle.idx];
    Assert(resource.references > 0);
    if (--resource.references)
        return;
    for (const auto& mesh : resource.meshes)
    {
        handle_destroy_deferred(mesh.mesh.vertex_buffer_handle);
        handle_destroy_deferred(mesh.mesh.index_buffer_handle);
        handle_destroy_deferred(mesh.triangle_bvh);
    }
    resource.meshes = {};
    resource.entries = {};
    resource.view = {};
}

} // namespace render
