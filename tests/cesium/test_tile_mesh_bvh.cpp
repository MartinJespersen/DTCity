#include "../test_inc.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/container.hpp"
#include "city/tile_bvh.hpp"
#include "render/render.hpp"
#include "shaders/shader_shared.h"

TEST_CASE("tile mesh BVH entry bounds use only indexed vertices")
{
    render::TileVertex vertices[4] = {};
    vertices[0].pos = {-8.0f, -3.0f, -4.0f};
    vertices[1].pos = {-2.0f, -9.0f, -6.0f};
    vertices[2].pos = {-5.0f, -1.0f, -2.0f};
    vertices[3].pos = {1000.0f, 1000.0f, 1000.0f}; // Unreferenced vertex must not expand the bounds.
    const U32 indices[] = {2, 0, 1};

    city::TileMeshBvhEntry entry = city::tile_mesh_bvh_entry_create(
        {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)}, {}, 7);

    CHECK(entry.bounds.min.x == -8.0f);
    CHECK(entry.bounds.min.y == -9.0f);
    CHECK(entry.bounds.min.z == -6.0f);
    CHECK(entry.bounds.max.x == -2.0f);
    CHECK(entry.bounds.max.y == -1.0f);
    CHECK(entry.bounds.max.z == -2.0f);
    CHECK(entry.triangle_count == 1);
    CHECK(entry.mesh_revision == 7);
    CHECK(entry.has_mesh_transform == false);
    CHECK(entry.handle.idx == 0); // Owner registration is separate from worker preparation.
    CHECK(entry.triangle_bvh_buffer.u64 == 0);
}

TEST_CASE("tile mesh BVH entry follows the final mesh and its resource version")
{
    render::TileVertex vertices[3] = {};
    vertices[0].pos = {1.0f, 2.0f, 3.0f};
    vertices[1].pos = {4.0f, 2.0f, 3.0f};
    vertices[2].pos = {1.0f, 5.0f, 3.0f};
    const U32 indices[] = {0, 1, 2};
    render::MeshHandle mesh = {};
    mesh.vertex_buffer_handle.u64 = 11;
    mesh.vertex_buffer_handle.gen_id = 2;
    mesh.vertex_buffer_handle.type = render::HandleType::Buffer;
    mesh.index_buffer_handle.u64 = 12;
    mesh.index_buffer_handle.gen_id = 3;
    mesh.index_buffer_handle.type = render::HandleType::Buffer;

    city::TileMeshBvhEntry original = city::tile_mesh_bvh_entry_create(
        {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)}, mesh, 4);
    vertices[2].pos.z = 12.0f; // Simulate the final result of mesh processing.
    mesh.vertex_buffer_handle.gen_id++;
    city::TileMeshBvhEntry replacement = city::tile_mesh_bvh_entry_create(
        {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)}, mesh, 5);

    CHECK(original.bounds.max.z == 3.0f);
    CHECK(replacement.bounds.max.z == 12.0f);
    CHECK(original.mesh_revision == 4);
    CHECK(replacement.mesh_revision == 5);
    CHECK(original.mesh.vertex_buffer_handle.gen_id == 2);
    CHECK(replacement.mesh.vertex_buffer_handle.gen_id == 3);
    CHECK(replacement.mesh.vertex_buffer_handle.u64 == 11);
    CHECK(replacement.mesh.index_buffer_handle.u64 == 12);
    CHECK(replacement.mesh.index_buffer_handle.gen_id == 3);
    CHECK(replacement.mesh.index_buffer_handle.type == render::HandleType::Buffer);
}

TEST_CASE("tile mesh BVH entry has no triangles for an empty index buffer")
{
    city::TileMeshBvhEntry entry = city::tile_mesh_bvh_entry_create({}, {}, {}, 0);
    CHECK(entry.triangle_count == 0);
    CHECK(entry.bounds.min.x == 0.0f);
    CHECK(entry.bounds.max.x == 0.0f);
    CHECK(entry.mesh.vertex_buffer_handle.u64 == 0);
    CHECK(entry.mesh.index_buffer_handle.u64 == 0);
}

TEST_CASE("tile mesh BVH build covers every triangle with enclosing bounds and unchanged winding")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    constexpr U32 triangle_count = 37;
    render::TileVertex vertices[triangle_count * 3] = {};
    U32 indices[triangle_count * 3] = {};

    // Exercise each split axis and coincident centroids, including zero-area
    // triangles. A non-power-of-two count also exercises uneven partitions.
    for (U32 scenario = 0; scenario < 4; ++scenario)
    {
        CAPTURE(scenario);
        Temp temp = temp_begin(arena);
        defer(temp_end(temp));
        for (U32 triangle_idx = 0; triangle_idx < triangle_count; ++triangle_idx)
        {
            for (U32 corner = 0; corner < 3; ++corner)
            {
                U32 index = triangle_idx * 3 + corner;
                vertices[index].pos = {};
                if (scenario < 3)
                {
                    vertices[index].pos[scenario] = (F32)triangle_idx * 10.0f - 200.0f;
                    vertices[index].pos[(scenario + 1) % 3] = corner == 1 ? 2.0f : 0.0f;
                    vertices[index].pos[(scenario + 2) % 3] = corner == 2 ? 3.0f : 0.0f;
                }
                // Reverse the input triangle order and rotate its corner order.
                indices[index] = (triangle_count - 1 - triangle_idx) * 3 + (corner + 1) % 3;
            }
        }
        city::TileMeshBvhBuild build = city::tile_mesh_bvh_build(
            arena, {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)});
        REQUIRE(build.nodes.size > 1);
        REQUIRE(build.nodes.size <= triangle_count * 2 - 1);
        REQUIRE(build.indices.size == ArrayCount(indices));
        CHECK(build.indices.data != indices);

        U32 node_references[triangle_count * 2 - 1] = {};
        U32 triangle_references[triangle_count] = {};
        U32 slot_references[triangle_count] = {};
        node_references[0] = 1;
        for (U32 node_idx = 0; node_idx < build.nodes.size; ++node_idx)
        {
            const shader::TileBvhGpuNode& node = build.nodes.data[node_idx];
            REQUIRE(node_references[node_idx] == 1);
            if (node.count == 0)
            {
                REQUIRE(node.first > node_idx);
                REQUIRE(node.first + 1 < build.nodes.size);
                for (U32 child_idx = node.first; child_idx < node.first + 2; ++child_idx)
                {
                    ++node_references[child_idx];
                    const shader::TileBvhGpuNode& child = build.nodes.data[child_idx];
                    for (U32 axis = 0; axis < 3; ++axis)
                    {
                        CHECK(node.bounds_min[axis] <= child.bounds_min[axis]);
                        CHECK(node.bounds_max[axis] >= child.bounds_max[axis]);
                    }
                }
            }
            else
            {
                CHECK(node.count <= 8);
                REQUIRE(node.first + node.count <= triangle_count);
                for (U32 slot = node.first; slot < node.first + node.count; ++slot)
                {
                    ++slot_references[slot];
                    U32 source_triangle = build.indices.data[(U64)slot * 3] / 3;
                    REQUIRE(source_triangle < triangle_count);
                    ++triangle_references[source_triangle];
                    for (U32 corner = 0; corner < 3; ++corner)
                    {
                        U32 vertex_idx = build.indices.data[slot * 3 + corner];
                        REQUIRE(vertex_idx == source_triangle * 3 + (corner + 1) % 3);
                        for (U32 axis = 0; axis < 3; ++axis)
                        {
                            CHECK(node.bounds_min[axis] <= vertices[vertex_idx].pos[axis]);
                            CHECK(node.bounds_max[axis] >= vertices[vertex_idx].pos[axis]);
                        }
                    }
                }
            }
        }
        for (U32 triangle_idx = 0; triangle_idx < triangle_count; ++triangle_idx)
        {
            CHECK(triangle_references[triangle_idx] == 1);
            CHECK(slot_references[triangle_idx] == 1);
            for (U32 corner = 0; corner < 3; ++corner)
                CHECK(indices[triangle_idx * 3 + corner] ==
                      (triangle_count - 1 - triangle_idx) * 3 + (corner + 1) % 3);
        }
    }
}

TEST_CASE("tile mesh BVH build handles empty and single-triangle meshes")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    city::TileMeshBvhBuild empty = city::tile_mesh_bvh_build(arena, {}, {});
    CHECK(empty.nodes.size == 0);
    CHECK(empty.indices.size == 0);

    render::TileVertex vertices[3] = {};
    vertices[0].pos = {-2.0f, -4.0f, -6.0f};
    vertices[1].pos = {3.0f, -1.0f, 2.0f};
    vertices[2].pos = {1.0f, 5.0f, -3.0f};
    const U32 indices[] = {2, 0, 1};
    city::TileMeshBvhBuild build = city::tile_mesh_bvh_build(
        arena, {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)});
    REQUIRE(build.nodes.size == 1);
    REQUIRE(build.indices.size == 3);
    CHECK(build.nodes.data[0].first == 0);
    CHECK(build.nodes.data[0].count == 1);
    for (U32 axis = 0; axis < 3; ++axis)
        CHECK(build.indices.data[axis] == indices[axis]);
    CHECK(build.nodes.data[0].bounds_min[0] == -2.0f);
    CHECK(build.nodes.data[0].bounds_min[1] == -4.0f);
    CHECK(build.nodes.data[0].bounds_min[2] == -6.0f);
    CHECK(build.nodes.data[0].bounds_max[0] == 3.0f);
    CHECK(build.nodes.data[0].bounds_max[1] == 5.0f);
    CHECK(build.nodes.data[0].bounds_max[2] == 2.0f);
}
