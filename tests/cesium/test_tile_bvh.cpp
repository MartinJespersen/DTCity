#include "../test_inc.hpp"
#include "../render/tile_bvh_test_backend.hpp"
#include "base/base_allocator.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_container.hpp"
#include "base/base_math.h"
#include "os_core/os_core.hpp"
#include "base/container_templates.hpp"
#include <cstdio>
#include "base/container.hpp"
#include "city/tile_bvh.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include <glm/ext/matrix_float4x4.hpp>

TEST_CASE("tile BVH queues changes and publishes only completed versions")
{
    Allocator allocator = Allocator::create();
    city::TileBvh bvh(&allocator);
    defer(bvh.destroy());
    auto& backend = tile_bvh_test::backend;
    city::TileMeshBvhEntry entry = {};
    entry.triangle_count = 1;
    entry.query_enabled = true;
    entry.mesh_revision = 2;
    entry.bounds.min = {-1, -2, -3};
    entry.bounds.max = {4, 5, 6};
    entry.mesh.vertex_buffer_handle = {reinterpret_cast<void*>(101), 1, render::HandleType::Buffer};
    entry.mesh.index_buffer_handle = {reinterpret_cast<void*>(102), 1, render::HandleType::Buffer};
    entry.triangle_bvh_buffer = {reinterpret_cast<void*>(103), 1, render::HandleType::Buffer};
    // Represent the tile batch's original ownership.
    for (U32 index = 101; index <= 103; ++index)
        backend.buffer_references[index] = 1;

    U32 before = backend.created;
    entry.handle = bvh.insert(entry);
    REQUIRE(entry.handle.idx != 0);
    CHECK(backend.created == before); // Callbacks queue; update starts the worker work.
    CHECK(bvh.update({}, {}).gen_id == 0);
    REQUIRE(backend.created == before + 1);
    U32 original_version = backend.created;
    CHECK(bvh.update({}, {}).gen_id == 0);
    CHECK(backend.created == original_version);
    backend.resources[original_version].ready = true;
    render::TileBvhHandle ready = bvh.update({}, {});
    REQUIRE(ready.idx == original_version);

    // A submitted query can keep the preceding immutable version alive.
    render::tile_bvh_resource_retain(ready);
    city::TileMeshBvhEntry replacement = entry;
    replacement.mesh_revision = 3;
    replacement.has_mesh_transform = true;
    replacement.mesh_to_city = glm::mat4(1.0f);
    replacement.mesh_to_city[3][2] = 10.0f;
    replacement.mesh.vertex_buffer_handle.u64 = 104;
    backend.buffer_references[104] = 1;
    bvh.replace(replacement);
    // Simulate the tile releasing its old vertex buffer after GPU completion.
    render::handle_destroy_deferred(entry.mesh.vertex_buffer_handle);
    CHECK(backend.buffer_references[101] == 1); // Original BVH version still retains it.
    CHECK(bvh.update({}, {}).idx == original_version);
    U32 replacement_version = backend.created;
    const auto& packed = backend.resources[replacement_version].entries.data[0];
    CHECK(packed.city_to_mesh_rows[2][3] == -10.0f);
    CHECK(packed.vertex_stride_bytes == sizeof(render::TileVertex));
    CHECK(packed.mesh_buffer_index == 0);
    CHECK(packed.entry_handle_idx == entry.handle.idx);
    CHECK(packed.entry_handle_gen_id == entry.handle.gen_id);

    // Outdated worker results cannot overwrite the current mesh revision.
    bvh.replace(entry);
    backend.resources[replacement_version].ready = true;
    CHECK(bvh.update({}, {}).idx == replacement_version);
    CHECK(backend.created == replacement_version);
    CHECK(backend.buffer_references[101] == 1);
    render::tile_bvh_resource_destroy(ready);
    CHECK(backend.buffer_references[101] == 0);

    bvh.remove(entry.handle);
    render::handle_destroy_deferred(replacement.mesh.vertex_buffer_handle);
    render::handle_destroy_deferred(entry.mesh.index_buffer_handle);
    render::handle_destroy_deferred(entry.triangle_bvh_buffer);
    CHECK(bvh.update({}, {}).idx == replacement_version);
    U32 empty_version = backend.created;
    CHECK(backend.resources[empty_version].view.entry_count == 0);
    CHECK(bvh.update({}, {}).idx == empty_version);
    for (U32 index = 101; index <= 104; ++index)
        CHECK(backend.buffer_references[index] == 0);
}

TEST_CASE("tile BVH rejects reused handles and retries a busy worker queue")
{
    Allocator allocator = Allocator::create();
    city::TileBvh bvh(&allocator);
    defer(bvh.destroy());
    auto& backend = tile_bvh_test::backend;
    city::TileMeshBvhEntry empty = {};
    PoolHandle old = bvh.insert(empty);
    bvh.remove(old);
    PoolHandle current = bvh.insert(empty);
    CHECK(current.idx == old.idx);
    CHECK(current.gen_id != old.gen_id);
    empty.handle = old;
    empty.triangle_count = 99;
    bvh.replace(empty);
    bvh.remove(old);
    U32 before = backend.created;
    backend.reject_submission = true;
    CHECK(bvh.update({}, {}).gen_id == 0);
    CHECK(backend.created == before);
    backend.reject_submission = false;
    CHECK(bvh.update({}, {}).gen_id == 0);
    REQUIRE(backend.created == before + 1);
    CHECK(backend.resources[backend.created].view.entry_count == 0);
    CHECK(bvh.update({}, {}).gen_id != 0);
}

TEST_CASE("tile BVH preserves changes queued during an outstanding upload")
{
    Allocator allocator = Allocator::create();
    city::TileBvh bvh(&allocator);
    defer(bvh.destroy());
    auto& backend = tile_bvh_test::backend;
    city::TileMeshBvhEntry entry = {};
    entry.triangle_count = 1;
    entry.query_enabled = true;
    entry.mesh.vertex_buffer_handle = {reinterpret_cast<void*>(111), 1, render::HandleType::Buffer};
    entry.mesh.index_buffer_handle = {reinterpret_cast<void*>(112), 1, render::HandleType::Buffer};
    entry.triangle_bvh_buffer = {reinterpret_cast<void*>(113), 1, render::HandleType::Buffer};
    entry.handle = bvh.insert(entry);
    CHECK(bvh.update({}, {}).gen_id == 0);
    U32 pending = backend.created;
    bvh.remove(entry.handle);
    CHECK(bvh.update({}, {}).gen_id == 0);
    CHECK(backend.created == pending);
    backend.resources[pending].ready = true;
    CHECK(bvh.update({}, {}).idx == pending);
    REQUIRE(backend.created == pending + 1);
    CHECK(backend.resources[backend.created].view.entry_count == 0);
    bvh.destroy();
    for (U32 index = 111; index <= 113; ++index)
        CHECK(backend.buffer_references[index] == 0);
}

TEST_CASE("tile BVH outer tree keeps overlapping mesh bounds reachable")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    constexpr U32 mesh_count = 29;
    Rng3F32 bounds[mesh_count] = {};
    for (U32 index = 0; index < mesh_count; ++index)
    {
        // Wide boxes overlap adjacent meshes; centers alone cannot describe them.
        bounds[index].min = {(F32)index * 3 - 4, -2, -(F32)index};
        bounds[index].max = {(F32)index * 3 + 4, 2, (F32)index + 1};
    }
    render::TileBvhBuild tree = render::tile_bvh_nodes_build(arena, {bounds, mesh_count}, 4);
    REQUIRE(tree.nodes.size > 1);
    REQUIRE(tree.item_indices.size == mesh_count);
    // Compare vertical-ray candidates against a brute-force box scan, including misses.
    for (S32 ray_x = -6; ray_x <= 90; ++ray_x)
    {
        U32 stack[mesh_count * 2] = {0};
        U32 stack_count = 1;
        U32 candidates[mesh_count] = {};
        while (stack_count)
        {
            const auto& node = tree.nodes.data[stack[--stack_count]];
            if (ray_x < node.bounds_min[0] || ray_x > node.bounds_max[0] ||
                0 < node.bounds_min[1] || 0 > node.bounds_max[1])
                continue;
            if (node.count)
            {
                REQUIRE(node.count <= 4);
                REQUIRE(node.first + node.count <= tree.item_indices.size);
                for (U32 offset = 0; offset < node.count; ++offset)
                {
                    U32 source_index = tree.item_indices.data[node.first + offset];
                    REQUIRE(source_index < mesh_count);
                    if (ray_x >= bounds[source_index].min.x && ray_x <= bounds[source_index].max.x)
                        ++candidates[source_index];
                }
            }
            else
            {
                REQUIRE(node.first + 1 < tree.nodes.size);
                REQUIRE(stack_count + 2 <= ArrayCount(stack));
                stack[stack_count++] = node.first;
                stack[stack_count++] = node.first + 1;
            }
        }
        for (U32 index = 0; index < mesh_count; ++index)
            CHECK(candidates[index] == (U32)(ray_x >= bounds[index].min.x && ray_x <= bounds[index].max.x));
    }
}

TEST_CASE("tile BVH registration handles survive growth and slot reuse")
{
    Allocator allocator = Allocator::create();
    city::TileBvh bvh(&allocator);
    defer(bvh.destroy());
    PoolHandle handles[96] = {};
    for (U32 index = 0; index < ArrayCount(handles); ++index)
    {
        handles[index] = bvh.insert({});
        CHECK(handles[index].idx == index + 1);
    }
    bvh.remove(handles[44]);
    city::TileMeshBvhEntry entry = {};
    entry.triangle_count = 1;
    entry.query_enabled = true;
    entry.mesh.vertex_buffer_handle = {reinterpret_cast<void*>(121), 1, render::HandleType::Buffer};
    entry.mesh.index_buffer_handle = {reinterpret_cast<void*>(122), 1, render::HandleType::Buffer};
    entry.triangle_bvh_buffer = {reinterpret_cast<void*>(123), 1, render::HandleType::Buffer};
    PoolHandle reused = bvh.insert(entry);
    CHECK(reused.idx == handles[44].idx);
    CHECK(reused.gen_id != handles[44].gen_id);
    // Removing the stale generation must leave the new queryable mesh registered.
    bvh.remove(handles[44]);
    CHECK(bvh.update({}, {}).gen_id == 0);
    auto& backend = tile_bvh_test::backend;
    U32 version = backend.created;
    CHECK(backend.resources[version].view.entry_count == 1);
    backend.resources[version].ready = true;
    CHECK(bvh.update({}, {}).idx == version);
    // Batch removal accepts all original handles; the reused generation remains.
    bvh.update({}, {handles, ArrayCount(handles)});
    CHECK(backend.resources[backend.created].view.entry_count == 1);
    bvh.destroy();
    for (U32 index = 121; index <= 123; ++index)
        CHECK(backend.buffer_references[index] == 0);
}

TEST_CASE("tile BVH includes selected meshes while cached parents stay registered")
{
    Allocator allocator = Allocator::create();
    city::TileBvh bvh(&allocator);
    defer(bvh.destroy());
    auto& backend = tile_bvh_test::backend;
    city::TileMeshBvhEntry entries[2] = {};
    for (U32 index = 0; index < 2; ++index)
    {
        auto& entry = entries[index];
        entry.triangle_count = 1;
        render::Handle* buffers[] = {&entry.mesh.vertex_buffer_handle, &entry.mesh.index_buffer_handle,
                                    &entry.triangle_bvh_buffer};
        for (U32 buffer = 0; buffer < ArrayCount(buffers); ++buffer)
        {
            render::Handle& handle = *buffers[buffer];
            handle.u64 = 141 + (U64)index * 3 + buffer;
            handle.gen_id = 1;
            handle.type = render::HandleType::Buffer;
            backend.buffer_references[handle.u64] = 1; // Loaded tile owns the cached buffers.
        }
        entry.handle = bvh.insert(entry);
    }

    // A loaded tile starts outside the query hierarchy. Hidden mesh replacements
    // must not rebuild an unchanged outer BVH.
    bvh.update({}, {});
    U32 empty_version = backend.created;
    REQUIRE(backend.resources[empty_version].view.entry_count == 0);
    REQUIRE(bvh.update({}, {}).idx == empty_version);
    ++entries[1].mesh_revision;
    bvh.replace(entries[1]);
    CHECK(bvh.update({}, {}).idx == empty_version);
    CHECK(backend.created == empty_version);

    entries[0].query_enabled = true;
    bvh.replace(entries[0]);
    CHECK(bvh.update({}, {}).idx == empty_version);
    U32 parent_version = backend.created;
    REQUIRE(backend.resources[parent_version].view.entry_count == 1);
    CHECK(backend.resources[parent_version].entries.data[0].entry_handle_idx == entries[0].handle.idx);
    backend.resources[parent_version].ready = true;
    render::TileBvhHandle parent = bvh.update({}, {});
    render::tile_bvh_resource_retain(parent); // An older GPU batch still uses this parent.

    // The detailed child becomes drawn while the parent remains loaded in cache.
    entries[0].query_enabled = false;
    entries[1].query_enabled = true;
    CHECK(bvh.update({entries, 2}, {}).idx == parent_version);
    U32 child_version = backend.created;
    REQUIRE(backend.resources[child_version].view.entry_count == 1);
    CHECK(backend.resources[child_version].entries.data[0].entry_handle_idx == entries[1].handle.idx);
    backend.resources[child_version].ready = true;
    CHECK(bvh.update({}, {}).idx == child_version);
    CHECK(backend.resources[parent_version].references == 1);
    CHECK(bvh.update({}, {}).idx == child_version);
    CHECK(backend.created == child_version);

    entries[1].query_enabled = false;
    bvh.replace(entries[1]);
    bvh.update({}, {});
    U32 hidden_version = backend.created;
    CHECK(backend.resources[hidden_version].view.entry_count == 0);
    CHECK(bvh.update({}, {}).idx == hidden_version);
    render::tile_bvh_resource_destroy(parent);
    for (U32 buffer = 141; buffer <= 146; ++buffer)
        CHECK(backend.buffer_references[buffer] == 2); // Tile + registry; no query version.
    bvh.destroy();
    for (U32 buffer = 141; buffer <= 146; ++buffer)
    {
        CHECK(backend.buffer_references[buffer] == 1);
        render::Handle handle = {};
        handle.u64 = buffer;
        handle.gen_id = 1;
        handle.type = render::HandleType::Buffer;
        render::handle_destroy_deferred(handle);
    }
}

TEST_CASE("tile BVH builds a large streamed mesh set")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    constexpr U32 count = 10000;
    Buffer<Rng3F32> bounds = buffer_alloc<Rng3F32>(arena, count);
    for (U32 index = 0; index < count; ++index)
    {
        U32 row = index / 100;
        F32 x = (F32)(index % 100) * 10;
        F32 y = (F32)row * 10;
        bounds.data[index].min = {x, y, -100};
        bounds.data[index].max = {x + 8, y + 8, 20};
    }
    U64 started = os_now_microseconds();
    render::TileBvhBuild tree = render::tile_bvh_nodes_build(arena, {bounds.data, count}, 4);
    U64 elapsed_us = os_now_microseconds() - started;
    std::printf("Tile BVH rebuild: %u meshes, %llu nodes, %.3f ms (CPU test build)\n",
        count, (unsigned long long)tree.nodes.size, (F64)elapsed_us / 1000.0);
    REQUIRE(tree.nodes.size > 1);
    REQUIRE(tree.item_indices.size == count);
    CHECK(tree.nodes.data[0].bounds_min[0] == 0);
    CHECK(tree.nodes.data[0].bounds_max[0] == 998);
    CHECK(tree.nodes.data[0].bounds_max[1] == 998);
    Buffer<U8> visited = buffer_alloc<U8>(arena, count);
    for (U32 source : tree.item_indices)
    {
        REQUIRE(source < count);
        CHECK(visited.data[source] == 0);
        visited.data[source] = 1;
    }
}
