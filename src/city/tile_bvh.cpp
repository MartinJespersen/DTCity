#include "tile_bvh.hpp"
#include "base/base_core.hpp"
#include "base/base_profile.hpp"
#include "base/base_arena.hpp"
#include "base/base_math.h"
#include "base/base_allocator_templates.hpp"
#include "base/base_allocator.hpp"
#include "base/base_container.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include "shaders/shader_shared.h"
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <utility>
#include <cstddef>

namespace city
{

TileMeshBvhBuild
tile_mesh_bvh_build(Arena* arena, Buffer<const render::TileVertex> vertices, Buffer<const U32> indices)
{
    Assert(indices.size % 3 == 0);
    Assert(indices.size <= max_U32);
    TileMeshBvhBuild result = {};
    U32 triangle_count = (U32)(indices.size / 3);
    if (triangle_count == 0)
        return result;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<Rng3F32> triangle_bounds = buffer_alloc<Rng3F32>(scratch.arena, triangle_count);
    for (U32 triangle_idx = 0; triangle_idx < triangle_count; ++triangle_idx)
    {
        Rng3F32& bounds = triangle_bounds.data[triangle_idx];
        for (U32 corner = 0; corner < 3; ++corner)
        {
            U32 vertex_idx = indices.data[(U64)triangle_idx * 3 + corner];
            Assert(vertex_idx < vertices.size);
            const glm::vec3& position = vertices.data[vertex_idx].pos;
            for (U32 axis = 0; axis < 3; ++axis)
            {
                F32 value = position[axis];
                bounds.min.v[axis] = corner == 0 ? value : Min(bounds.min.v[axis], value);
                bounds.max.v[axis] = corner == 0 ? value : Max(bounds.max.v[axis], value);
            }
        }
    }
    render::TileBvhBuild tree = render::tile_bvh_nodes_build(
        arena, {triangle_bounds.data, triangle_bounds.size}, 8);
    result.nodes = tree.nodes;
    result.indices = buffer_alloc<U32>(arena, indices.size);
    // Match leaf ranges while preserving triangle winding and input geometry.
    for (U32 triangle_idx = 0; triangle_idx < triangle_count; ++triangle_idx)
    {
        U64 source_index = (U64)tree.item_indices.data[triangle_idx] * 3;
        for (U32 corner = 0; corner < 3; ++corner)
            result.indices.data[(U64)triangle_idx * 3 + corner] = indices.data[source_index + corner];
    }
    return result;
}

TileMeshBvhEntry
tile_mesh_bvh_entry_create(Buffer<const render::TileVertex> vertices, Buffer<const U32> indices,
                           render::MeshHandle mesh, U64 mesh_revision)
{
    Assert(indices.size % 3 == 0);
    Assert(indices.size / 3 <= max_U32);

    TileMeshBvhEntry entry = {};
    entry.mesh = mesh;
    entry.mesh_revision = mesh_revision;
    entry.triangle_count = (U32)(indices.size / 3);

    // Only referenced vertices contribute to the query bounds. The worker has
    // already transformed positions to city-local space and applied road processing.
    for (U64 index_idx = 0; index_idx < indices.size; ++index_idx)
    {
        U32 vertex_idx = indices.data[index_idx];
        Assert(vertex_idx < vertices.size);
        const glm::vec3& position = vertices.data[vertex_idx].pos;
        if (index_idx == 0)
        {
            entry.bounds.min = {position.x, position.y, position.z};
            entry.bounds.max = entry.bounds.min;
        }
        else
        {
            entry.bounds.min.x = Min(entry.bounds.min.x, position.x);
            entry.bounds.min.y = Min(entry.bounds.min.y, position.y);
            entry.bounds.min.z = Min(entry.bounds.min.z, position.z);
            entry.bounds.max.x = Max(entry.bounds.max.x, position.x);
            entry.bounds.max.y = Max(entry.bounds.max.y, position.y);
            entry.bounds.max.z = Max(entry.bounds.max.z, position.z);
        }
    }

    return entry;
}

TileBvh::TileBvh(Allocator* allocator) : allocator(allocator), dirty(true)
{
}

void
TileBvh::destroy()
{
    for (U32 index = 1; index <= this->slot_count; ++index)
    {
        if (this->slots.data[index].occupied)
            this->_entry_buffers_release(this->slots.data[index].entry);
    }
    render::tile_bvh_resource_destroy(this->ready_resource);
    render::tile_bvh_resource_destroy(this->pending_resource);
    this->ready_resource = {};
    this->pending_resource = {};
    this->slots = {};
    this->slot_count = 0;
    this->free_slot = 0;
    this->dirty = true;
}

PoolHandle
TileBvh::insert(const TileMeshBvhEntry& entry)
{
    U32 index = this->free_slot;
    if (index)
        this->free_slot = this->slots.data[index].next_free;
    else
    {
        index = this->slot_count + 1;
        if (index >= this->slots.size)
        {
            U32 capacity = Max(32u, this->slots.size * 2);
            Array<Slot> grown(this->allocator, capacity);
            for (U32 slot_index = 1; slot_index <= this->slot_count; ++slot_index)
                grown.data[slot_index] = this->slots.data[slot_index];
            this->slots = std::move(grown);
        }
        this->slot_count = index;
        this->slots.data[index].generation = 1;
    }
    Slot& slot = this->slots.data[index];
    slot.entry = entry;
    slot.entry.handle = {index, slot.generation};
    slot.next_free = 0;
    slot.occupied = true;
    this->_entry_buffers_retain(slot.entry);
    this->dirty = this->dirty || (slot.entry.query_enabled && slot.entry.triangle_count > 0);
    return slot.entry.handle;
}

void
TileBvh::replace(const TileMeshBvhEntry& entry)
{
    if (entry.handle.idx == 0 || entry.handle.idx > this->slot_count)
        return;
    Slot& slot = this->slots.data[entry.handle.idx];
    if (!slot.occupied || slot.generation != entry.handle.gen_id ||
        entry.mesh_revision < slot.entry.mesh_revision)
        return;
    // Acquire the replacement before releasing the old registration. Buffers may be shared.
    B32 affects_queries = (slot.entry.query_enabled && slot.entry.triangle_count > 0) ||
                          (entry.query_enabled && entry.triangle_count > 0);
    this->_entry_buffers_retain(entry);
    this->_entry_buffers_release(slot.entry);
    slot.entry = entry;
    this->dirty = this->dirty || affects_queries;
}

void
TileBvh::remove(PoolHandle handle)
{
    if (handle.idx == 0 || handle.idx > this->slot_count)
        return;
    Slot& slot = this->slots.data[handle.idx];
    if (!slot.occupied || slot.generation != handle.gen_id)
        return;
    this->dirty = this->dirty || (slot.entry.query_enabled && slot.entry.triangle_count > 0);
    this->_entry_buffers_release(slot.entry);
    slot.entry = {};
    slot.occupied = false;
    ++slot.generation;
    Assert(slot.generation != 0);
    slot.next_free = this->free_slot;
    this->free_slot = handle.idx;
}

render::TileBvhHandle
TileBvh::update(Buffer<const TileMeshBvhEntry> changed_entries,
                Buffer<const PoolHandle> removed_entries)
{
    prof_scope_marker;
    for (const TileMeshBvhEntry& entry : changed_entries)
        this->replace(entry);
    for (PoolHandle handle : removed_entries)
        this->remove(handle);

    // Publish only complete versions. Keep the preceding version alive until then.
    render::TileBvhResourceView view = {};
    if (this->pending_resource.gen_id && render::tile_bvh_resource_get(this->pending_resource, &view))
    {
        render::tile_bvh_resource_destroy(this->ready_resource);
        this->ready_resource = this->pending_resource;
        this->pending_resource = {};
    }
    if (!this->dirty || this->pending_resource.gen_id)
        return this->ready_resource;

    Arena* arena = this->allocator->arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    U32 count = 0;
    for (U32 index = 1; index <= this->slot_count; ++index)
        count += this->slots.data[index].occupied && this->slots.data[index].entry.query_enabled &&
                 this->slots.data[index].entry.triangle_count > 0;
    Buffer<Rng3F32> bounds = buffer_alloc<Rng3F32>(scratch.arena, count);
    Buffer<render::TileBvhGpuEntry> entries = buffer_alloc<render::TileBvhGpuEntry>(scratch.arena, count);
    Buffer<render::TileBvhMeshBuffers> meshes = buffer_alloc<render::TileBvhMeshBuffers>(scratch.arena, count);
    U32 mesh_index = 0;
    for (U32 index = 1; index <= this->slot_count; ++index)
    {
        const Slot& slot = this->slots.data[index];
        if (!slot.occupied || !slot.entry.query_enabled || slot.entry.triangle_count == 0)
            continue;
        const TileMeshBvhEntry& entry = slot.entry;
        bounds.data[mesh_index] = entry.bounds;
        meshes.data[mesh_index] = {entry.mesh, entry.triangle_bvh_buffer};
        render::TileBvhGpuEntry& gpu_entry = entries.data[mesh_index];
        gpu_entry = {};
        glm::mat4 city_to_mesh(1.0f);
        if (entry.has_mesh_transform)
            city_to_mesh = glm::affineInverse(entry.mesh_to_city);
        for (U32 row = 0; row < 3; ++row)
            for (U32 column = 0; column < 4; ++column)
                gpu_entry.city_to_mesh_rows[row][column] = city_to_mesh[column][row];
        gpu_entry.mesh_buffer_index = mesh_index;
        gpu_entry.vertex_position_offset_bytes = (U32)offsetof(render::TileVertex, pos);
        gpu_entry.vertex_stride_bytes = sizeof(render::TileVertex);
        gpu_entry.entry_handle_idx = entry.handle.idx;
        gpu_entry.entry_handle_gen_id = entry.handle.gen_id;
        ++mesh_index;
    }
    this->pending_resource = render::tile_bvh_resource_create(
        {bounds.data, bounds.size}, {entries.data, entries.size}, {meshes.data, meshes.size});
    if (this->pending_resource.gen_id)
        this->dirty = false;
    return this->ready_resource;
}

// Private helpers. Registration and each immutable GPU version own independent references.
void
TileBvh::_entry_buffers_retain(const TileMeshBvhEntry& entry)
{
    render::buffer_retain(entry.mesh.vertex_buffer_handle);
    render::buffer_retain(entry.mesh.index_buffer_handle);
    render::buffer_retain(entry.triangle_bvh_buffer);
}

void
TileBvh::_entry_buffers_release(const TileMeshBvhEntry& entry)
{
    render::handle_destroy_deferred(entry.mesh.vertex_buffer_handle);
    render::handle_destroy_deferred(entry.mesh.index_buffer_handle);
    render::handle_destroy_deferred(entry.triangle_bvh_buffer);
}

} // namespace city
