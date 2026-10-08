#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/cache.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "utility/utility_inc.hpp"
#include "async/async_inc.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/misc_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "gltfw/gltfw.hpp"
#include "osm/osm.hpp"
#include "cesium/cesium_tileset.hpp"
#include "city/city_inc.hpp"
#include "entrypoint.hpp"

namespace city
{

g_internal geometry::Quad2d
_to_glm_quad(city::RoadSegmentCorners* road)
{
    constexpr U32 CORNERS_COUNT = city::RoadSegmentCornerCoord_Count;
    glm::vec2 road_vertices[city::RoadSegmentCornerCoord_Count] = {};
    for (U32 r_i = 0; r_i < CORNERS_COUNT; r_i++)
    {
        Vec2F32 road_vert = road->corners[r_i];
        road_vertices[r_i] = glm::vec2(road_vert.x, road_vert.y);
    }

    geometry::Quad2d quad = {};
    for (U32 r_i = 0; r_i < CORNERS_COUNT; r_i++)
    {
        quad.v[r_i] = road_vertices[r_i];
    }
    return quad;
}

g_internal bool
_polygon_intersection_sat(geometry::Quad2d quad, geometry::Triangle2d face)
{
    constexpr U32 CORNERS_COUNT = ArrayCount(quad.v);
    // check every edge of road_segment
    for (U32 r_i = 0; r_i < ArrayCount(quad.v); r_i++)
    {
        glm::vec2 p0 = quad.v[r_i];
        glm::vec2 p1 = quad.v[(r_i + 1) % CORNERS_COUNT];
        glm::vec2 r_v = p0 - p1;
        glm::vec2 n = glm::vec2(-r_v.y, r_v.x);

        // find max interval for road segment points project to normal vector n
        float p2_dot = dot(quad.v[(r_i + 2) % CORNERS_COUNT] - p0, n);
        float p3_dot = dot(quad.v[(r_i + 3) % CORNERS_COUNT] - p0, n);

        float min_seg = Min(0.0, Min(p2_dot, p3_dot));
        float max_seg = Max(0.0, Max(p2_dot, p3_dot));

        // find max interval for triangle points project to normal vector n
        float min_face = Min(dot(face.v[0] - p0, n), Min(dot(face.v[1] - p0, n), dot(face.v[2] - p0, n)));
        float max_face = Max(dot(face.v[0] - p0, n), Max(dot(face.v[1] - p0, n), dot(face.v[2] - p0, n)));

        if (min_face > max_seg || max_face < min_seg)
        {
            return false;
        }
    }

    // check every edge of triangle
    constexpr U32 FACE_VERTEX_COUNT = ArrayCount(face.v);
    for (U32 t_i = 0; t_i < FACE_VERTEX_COUNT; t_i++)
    {
        glm::vec2 p0 = face.v[t_i];
        glm::vec2 p1 = face.v[(t_i + 1) % FACE_VERTEX_COUNT];
        glm::vec2 t_v = p0 - p1;
        glm::vec2 n = glm::vec2(-t_v.y, t_v.x);

        // find max interval for remaining triangle point project to normal vector n
        float opposite_dot = dot(face.v[(t_i + 2) % FACE_VERTEX_COUNT] - p0, n);
        float min_triangle = Min(0.0, opposite_dot);
        float max_triangle = Max(0.0, opposite_dot);

        float min_seg = Min(Min(dot(quad.v[0] - p0, n), dot(quad.v[1] - p0, n)),
                            Min(dot(quad.v[2] - p0, n), dot(quad.v[3] - p0, n)));
        float max_seg = Max(Max(dot(quad.v[0] - p0, n), dot(quad.v[1] - p0, n)),
                            Max(dot(quad.v[2] - p0, n), dot(quad.v[3] - p0, n)));

        if (min_triangle > max_seg || max_triangle < min_seg)
        {
            return false;
        }
    }

    return true;
}

bool
_face_bounds_overlap(Rng2F32 bounds, geometry::Triangle2d& face)
{
    glm::vec2 face_min = (glm::min)(face.v[0], (glm::min)(face.v[1], face.v[2]));
    glm::vec2 face_max = (glm::max)(face.v[0], (glm::max)(face.v[1], face.v[2]));

    return face_max.x >= bounds.min.x && face_min.x <= bounds.max.x && face_max.y >= bounds.min.y &&
           face_min.y <= bounds.max.y;
}

g_internal void
_assign_vertex_values(glm::vec2 v, TileVertexFace& face, geometry::Triangle2d& projected_tri,
                      render::TileVertex* out_vertex)
{
    glm::vec3 barycentric = geometry::barycentric_2d(v, projected_tri.v[0], projected_tri.v[1], projected_tri.v[2]);
    glm::vec3 interpolated_pos =
        barycentric.x * face.v[0].pos + barycentric.y * face.v[1].pos + barycentric.z * face.v[2].pos;
    glm::vec2 interpolated_uv = barycentric.x * glm::vec2(face.v[0].uv.x, face.v[0].uv.y) +
                                barycentric.y * glm::vec2(face.v[1].uv.x, face.v[1].uv.y) +
                                barycentric.z * glm::vec2(face.v[2].uv.x, face.v[2].uv.y);
    glm::vec2 interpolated_overlay_uv = barycentric.x * glm::vec2(face.v[0].overlay_uv.x, face.v[0].overlay_uv.y) +
                                        barycentric.y * glm::vec2(face.v[1].overlay_uv.x, face.v[1].overlay_uv.y) +
                                        barycentric.z * glm::vec2(face.v[2].overlay_uv.x, face.v[2].overlay_uv.y);

    out_vertex->pos = interpolated_pos;
    out_vertex->overlay_uv = interpolated_overlay_uv;
    out_vertex->uv = interpolated_uv;
    // TODO: the below fields should also change: eg. object_id could be known, road_segment_idx is no longer needed.
    out_vertex->road_segment_idx = 0;
}

render::TileMesh
_render_mesh_from_2d_mesh(Arena* arena, geometry::PolygonMesh2d& triangulated_poly, TileVertexFace& tri,
                          geometry::Triangle2d& projected_tri)
{
    Buffer<render::TileVertex> vertices = buffer_alloc<render::TileVertex>(arena, triangulated_poly.vertices.size);
    for (U32 vertex_idx = 0; vertex_idx < triangulated_poly.vertices.size; ++vertex_idx)
    {
        glm::vec2 v = triangulated_poly.vertices.data[vertex_idx];
        _assign_vertex_values(v, tri, projected_tri, &vertices.data[vertex_idx]);
    }

    render::TileMesh mesh = {
        .vertices = vertices,
        .indices = triangulated_poly.indices,
    };
    return mesh;
}

g_internal void
_mesh_append(Arena* arena, ChunkList<render::TileVertex>* vertices_chunk_list, ChunkList<U32>* indices_chunk_list,
             render::TileMesh& mesh, U32 road_idx)
{
    U32 base_vertex_idx = vertices_chunk_list->total_count;
    for (auto vert : mesh.vertices)
    {
        vert.road_segment_idx = road_idx;
        chunk_list_insert(arena, vertices_chunk_list, vert);
    }

    for (auto ind : mesh.indices)
    {
        U32 new_idx = ind + base_vertex_idx;
        chunk_list_insert(arena, indices_chunk_list, new_idx);
    }
}

g_internal render::TileMesh
_tessellate_tile_face_for_roads(Arena* arena, city::RoadSegmentNode* root, Buffer<city::RoadSegmentCorners> road_buffer,
                                Buffer<city::RoadSegmentNode*> node_stack, TileVertexFace& face)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(&arena, 1);
    constexpr U64 vertex_chunk_capacity = 256;
    constexpr U64 index_chunk_capacity = 512;
    ChunkList<render::TileVertex>* vertices_chunk_list =
        chunk_list_create<render::TileVertex>(scratch.arena, vertex_chunk_capacity);
    ChunkList<U32>* indices_chunk_list = chunk_list_create<U32>(scratch.arena, index_chunk_capacity);

    geometry::Triangle2d projected_tri = {};
    projected_tri.v[0] = glm::vec2(face.v[0].pos.x, face.v[0].pos.y);
    projected_tri.v[1] = glm::vec2(face.v[1].pos.x, face.v[1].pos.y);
    projected_tri.v[2] = glm::vec2(face.v[2].pos.x, face.v[2].pos.y);

    if (!root || road_buffer.size == 0)
    {
        return {};
    }

    // Collect candidate roads without modifying RoadSegmentNode::next. Tile
    // preparation can run concurrently, so the BVH itself must remain read-only.
    U64 node_stack_count = 0;
    Assert(node_stack.size > 0);
    node_stack.data[node_stack_count++] = root;

    constexpr U64 road_candidate_chunk_capacity = 32;
    ChunkList<geometry::Quad2d>* candidate_quads =
        chunk_list_create<geometry::Quad2d>(scratch.arena, road_candidate_chunk_capacity);
    ChunkList<U32>* candidate_road_indices = chunk_list_create<U32>(scratch.arena, road_candidate_chunk_capacity);
    while (node_stack_count > 0)
    {
        city::RoadSegmentNode* node = node_stack.data[--node_stack_count];
        if (!_face_bounds_overlap(node->bounds, projected_tri))
        {
            continue;
        }

        bool is_leaf = node->children[0] == nullptr;
        if (!is_leaf)
        {
            Assert(node_stack_count + 2 <= node_stack.size);
            node_stack.data[node_stack_count++] = node->children[1];
            node_stack.data[node_stack_count++] = node->children[0];
            continue;
        }

        for (U32 road_idx = node->start_idx; road_idx < node->end_idx; ++road_idx)
        {
            city::RoadSegmentCorners* road = &road_buffer.data[road_idx];
            geometry::Quad2d road_quad = _to_glm_quad(road);
            if (!_polygon_intersection_sat(road_quad, projected_tri))
            {
                continue;
            }

            F32 twice_area = 0.0f;
            for (U32 vertex_idx = 0; vertex_idx < ArrayCount(road_quad.v); ++vertex_idx)
            {
                glm::vec2 a = road_quad.v[vertex_idx];
                glm::vec2 b = road_quad.v[(vertex_idx + 1) % ArrayCount(road_quad.v)];
                twice_area += a.x * b.y - a.y * b.x;
            }

            geometry::Quad2d ccw_road_quad = road_quad;
            if (twice_area < 0.0f)
            {
                for (U32 vertex_idx = 0; vertex_idx < ArrayCount(ccw_road_quad.v); ++vertex_idx)
                {
                    ccw_road_quad.v[vertex_idx] = road_quad.v[ArrayCount(road_quad.v) - 1 - vertex_idx];
                }
            }

            chunk_list_insert(scratch.arena, candidate_quads, ccw_road_quad);
            chunk_list_insert(scratch.arena, candidate_road_indices, road_idx);
        }
    }

    if (candidate_quads->total_count == 0)
    {
        return {};
    }

    // Dense, low-detail tile faces can overlap a very large number of roads.
    // Leave those faces unchanged so tessellation cannot monopolize a worker
    // indefinitely and delay Cesium tile loading.
    constexpr U64 maximum_road_intersection_count = 100;
    if (candidate_quads->total_count > maximum_road_intersection_count)
    {
        return {};
    }

    Buffer<geometry::Quad2d> quad_buffer = buffer_from_chunk_list(scratch.arena, candidate_quads);
    Buffer<U32> road_index_buffer = buffer_from_chunk_list(scratch.arena, candidate_road_indices);

    Buffer<geometry::ClassifiedTriangle2d> partition =
        geometry::triangle_partition_by_quads(scratch.arena, projected_tri, quad_buffer);

    bool has_road_triangle = false;
    for (geometry::ClassifiedTriangle2d& classified_triangle : partition)
    {
        if (classified_triangle.region_idx != 0)
        {
            has_road_triangle = true;
            break;
        }
    }
    if (!has_road_triangle)
    {
        return {};
    }

    U32 triangle_indices[] = {0, 1, 2};
    for (geometry::ClassifiedTriangle2d& classified_triangle : partition)
    {
        geometry::PolygonMesh2d triangle_mesh_2d = {
            .vertices = {.data = classified_triangle.triangle.v, .size = ArrayCount(classified_triangle.triangle.v)},
            .indices = {.data = triangle_indices, .size = ArrayCount(triangle_indices)},
        };
        render::TileMesh triangle_mesh =
            _render_mesh_from_2d_mesh(scratch.arena, triangle_mesh_2d, face, projected_tri);

        U32 encoded_road_idx = 0;
        if (classified_triangle.region_idx != 0)
        {
            U32 candidate_idx = classified_triangle.region_idx - 1;
            Assert(candidate_idx < road_index_buffer.size);
            encoded_road_idx = road_index_buffer.data[candidate_idx] + 1;
        }
        _mesh_append(scratch.arena, vertices_chunk_list, indices_chunk_list, triangle_mesh, encoded_road_idx);
    }

    Buffer<render::TileVertex> vertices = buffer_from_chunk_list(arena, vertices_chunk_list);
    Buffer<U32> indices = buffer_from_chunk_list(arena, indices_chunk_list);
    render::TileMesh mesh = {.vertices = vertices, .indices = indices};
    return mesh;
}

g_internal bool
tesselate_roads(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, Bvh& bvh_result,
                render::TileMesh* out_mesh)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    ChunkList<U32>* indices_chunk_list = chunk_list_create<U32>(scratch.arena, 1000);
    ChunkList<render::TileVertex>* vertices_chunk_list = chunk_list_create<render::TileVertex>(scratch.arena, 1000);
    U32* original_vertex_to_output_idx = PushArray(scratch.arena, U32, vertices.size);
    B32* original_vertex_is_added = PushArray(scratch.arena, B32, vertices.size);
    bool has_road_classification = false;

    // All faces traverse the same BVH. Reuse one stack for the tile task.
    U64 node_stack_capacity = bvh_result.road_segment_buffer_sorted.size * 2;
    city::RoadSegmentNode** node_stack_data = PushArrayNoZero(scratch.arena, city::RoadSegmentNode*, node_stack_capacity);
    Buffer<city::RoadSegmentNode*> node_stack = {.data = node_stack_data, .size = node_stack_capacity};

    // iterate faces
    for (U32 i = 0; i < indices.size; i += 3)
    {
        city::TileVertexFace face_vertices = {};
        face_vertices.v[0] = vertices.data[indices.data[i]];
        face_vertices.v[1] = vertices.data[indices.data[i + 1]];
        face_vertices.v[2] = vertices.data[indices.data[i + 2]];

        render::TileMesh tessellated_mesh = _tessellate_tile_face_for_roads(
            scratch.arena, bvh_result.root, bvh_result.road_segment_buffer_sorted, node_stack, face_vertices);
        if (tessellated_mesh.indices.size)
        {
            has_road_classification = true;
            U32 idx_offset = vertices_chunk_list->total_count;

            for (U32 tessellated_index_idx = 0; tessellated_index_idx < tessellated_mesh.indices.size;
                 ++tessellated_index_idx)
            {
                U32 index = tessellated_mesh.indices.data[tessellated_index_idx] + idx_offset;
                chunk_list_insert(scratch.arena, indices_chunk_list, index);
            }

            chunk_list_from_buffer_append(scratch.arena, vertices_chunk_list, tessellated_mesh.vertices);
        }
        else
        {
            for (U32 face_vertex_idx = 0; face_vertex_idx < ArrayCount(face_vertices.v); face_vertex_idx++)
            {
                render::TileVertex* tv = &vertices.data[indices.data[i + face_vertex_idx]];

                U32 original_vertex_idx = indices.data[i + face_vertex_idx];
                if (!original_vertex_is_added[original_vertex_idx])
                {
                    U32 output_vertex_idx = (U32)vertices_chunk_list->total_count;
                    original_vertex_to_output_idx[original_vertex_idx] = output_vertex_idx;
                    original_vertex_is_added[original_vertex_idx] = true;

                    render::TileVertex* new_vertex = chunk_list_get_next(scratch.arena, vertices_chunk_list);
                    *new_vertex = *tv;
                }

                U32 index = original_vertex_to_output_idx[original_vertex_idx];
                chunk_list_insert(scratch.arena, indices_chunk_list, index);
            }
        }
    }

    render::TileMesh mesh = {};
    mesh.indices = buffer_from_chunk_list(arena, indices_chunk_list);
    mesh.vertices = buffer_from_chunk_list(arena, vertices_chunk_list);
    *out_mesh = mesh;
    return has_road_classification;
}

} // namespace city
