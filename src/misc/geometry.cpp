namespace geometry
{

glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch)
{
    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.z = sin(glm::radians(pitch));

    return glm::normalize(direction);
}

// Source - https://stackoverflow.com/a/1968345
// Posted by Gavin, modified by community. See post 'Timeline' for change history
// Retrieved 2026-07-20, License - CC BY-SA 3.0

// Returns 1 if the lines intersect, otherwise 0. In addition, if the lines
// intersect the intersection point may be stored in the floats i_x and i_y.
bool
_line_intersection(float p0_x, float p0_y, float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y, float* i_x, float* i_y)
{
    float s1_x, s1_y, s2_x, s2_y;
    s1_x = p1_x - p0_x;
    s1_y = p1_y - p0_y;
    s2_x = p3_x - p2_x;
    s2_y = p3_y - p2_y;

    float s, t;
    s = (-s1_y * (p0_x - p2_x) + s1_x * (p0_y - p2_y)) / (-s2_x * s1_y + s1_x * s2_y);
    t = (s2_x * (p0_y - p2_y) - s2_y * (p0_x - p2_x)) / (-s2_x * s1_y + s1_x * s2_y);

    if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
    {
        // Collision detected
        if (i_x != NULL)
            *i_x = p0_x + (t * s1_x);
        if (i_y != NULL)
            *i_y = p0_y + (t * s1_y);
        return 1;
    }

    return 0; // No collision
}

bool
line_intersection(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2* i)
{
    return _line_intersection(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, &i->x, &i->y);
}

glm::vec3
barycentric_2d(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    // 1. Calculate the common denominator using 2D cross product math (determinant)
    float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);

    // Guard against degenerate triangles (points in a straight line / zero area)
    if (glm::abs(denom) < 0.000001f)
    {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // 2. Compute the 2D barycentric weights (u, v, w)
    float u = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / denom;
    float v = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / denom;
    float w = 1.0f - u - v;

    return glm::vec3(u, v, w);
}

g_internal F32
tolerance_calculation(F32* v, U32 v_count, F32 relative_tolerance, F32 absolute_tolerance)
{
    F32 coordinate_scale = 0.0f;
    for (U32 i = 0; i < v_count; ++i)
    {
        coordinate_scale = (glm::max)(coordinate_scale, glm::abs(v[i]));
    }

    F32 tolerance = (glm::max)(absolute_tolerance, relative_tolerance * coordinate_scale);

    return tolerance;
}

g_internal F32
tolerance_calculation(std::initializer_list<F32> scale_values, F32 relative_tolerance, F32 absolute_tolerance)
{
    U32 scale_value_count = (U32)scale_values.size();
    F32 result = tolerance_calculation((F32*)scale_values.begin(), scale_value_count, relative_tolerance, absolute_tolerance);
    return result;
}

bool
intersect_segment_plane(glm::vec2 a, glm::vec2 b, Plane2d p, float& t, glm::vec2& q)
{
    // Compute the t value for the directed line ab intersecting the plane
    glm::dvec2 a_f64 = glm::dvec2((F64)a.x, (F64)a.y);
    glm::dvec2 b_f64 = glm::dvec2((F64)b.x, (F64)b.y);
    glm::dvec2 normal_f64 = glm::dvec2((F64)p.n.x, (F64)p.n.y);
    glm::dvec2 ab_f64 = b_f64 - a_f64;
    F64 t_f64 = ((F64)p.d - glm::dot(normal_f64, a_f64)) / glm::dot(normal_f64, ab_f64);
    t = (F32)t_f64;

    // If t in [0..1] compute and return intersection point
    if (t_f64 >= 0.0 && t_f64 <= 1.0)
    {
        glm::dvec2 intersection_f64 = a_f64 + t_f64 * ab_f64;
        q = glm::vec2((F32)intersection_f64.x, (F32)intersection_f64.y);
        return true;
    }
    // Else no intersection
    return false;
}

Plane2d
line_from_points(glm::vec2 a, glm::vec2 b)
{
    Plane2d plane = {};

    glm::vec2 dir = b - a;
    F32 dir_length_squared = glm::dot(dir, dir);
    if (dir_length_squared == 0.0f)
    {
        return plane;
    }

    glm::vec2 normal = glm::vec2(-dir.y, dir.x);
    plane.n = glm::normalize(normal);
    plane.d = glm::dot(plane.n, a);

    return plane;
}

// Classify point p to a plane thickened by a given thickness
// NOTE: In front is in the direction of normal
PlaneClassification
classify_point_on_plane(glm::vec2& p, Plane2d& line)
{
    // Compute signed distance of point from plane
    float dist = glm::dot(p, line.n) - line.d;
    // Classify p based on the signed distance
    F32 plane_thickness = tolerance_calculation({p.x, p.y});
    if (dist > plane_thickness)
        return PlaneClassification::InFrontOf;
    if (dist < -plane_thickness)
        return PlaneClassification::Behind;
    return PlaneClassification::On;
}

// Return value specifying whether the polygon ‘poly’ lies in front of,
// behind of, on, or straddles the plane ‘plane’
PolygonPlaneClassification
classify_polygon_to_plane(glm::vec2* poly, U32 poly_vert_count, Plane2d& plane)
{
    // Loop over all polygon vertices and count how many vertices
    // lie in front of and how many lie behind of the thickened plane
    int numInFront = 0, numBehind = 0;
    for (U32 i = 0; i < poly_vert_count; i++)
    {
        glm::vec2& p = poly[i];
        PlaneClassification classification = classify_point_on_plane(p, plane);
        switch (classification)
        {
            case PlaneClassification::InFrontOf: numInFront++; break;
            case PlaneClassification::Behind: numBehind++; break;
            case PlaneClassification::On: break;
        }
    }
    // If vertices on both sides of the plane, the polygon is straddling
    if (numBehind != 0 && numInFront != 0)
        return PolygonPlaneClassification::Straddling;
    // If one or more vertices in front of the plane and no vertices behind
    // the plane, the polygon lies in front of the plane
    if (numInFront != 0)
        return PolygonPlaneClassification::InFrontOf;
    // Ditto, the polygon lies behind the plane if no vertices in front of
    // the plane, and one or more vertices behind the plane
    if (numBehind != 0)
        return PolygonPlaneClassification::Behind;
    // All vertices lie on the plane so the polygon is coplanar with the plane
    return PolygonPlaneClassification::CoPlanar;
}

void
split_polygon(Arena* arena, glm::vec2* poly, U32 poly_vert_count, Plane2d& plane, Buffer<glm::vec2>* front_poly, Buffer<glm::vec2>* back_poly)
{
    AssertAlways(poly_vert_count >= 3);
    int num_front = 0, num_back = 0;
    glm::vec2* front_verts = PushArray(arena, glm::vec2, poly_vert_count + 2);
    glm::vec2* back_verts = PushArray(arena, glm::vec2, poly_vert_count + 2);
    // Test all edges (a, b) starting with edge from last to first vertex
    glm::vec2 a = poly[poly_vert_count - 1];
    PlaneClassification a_side = classify_point_on_plane(a, plane);
    // Loop over all edges given by vertex pair (n - 1, n)
    for (U32 n = 0; n < poly_vert_count; n++)
    {
        glm::vec2 b = poly[n];
        PlaneClassification b_side = classify_point_on_plane(b, plane);
        if (b_side == PlaneClassification::InFrontOf)
        {
            if (a_side == PlaneClassification::Behind)
            {
                // Edge (a, b) straddles, output intersection point to both sides
                glm::vec2 i = {};
                F32 t = {};
                bool intersect = intersect_segment_plane(a, b, plane, t, i);
                Assert(intersect);
                Assert(classify_point_on_plane(i, plane) == PlaneClassification::On);

                front_verts[num_front++] = back_verts[num_back++] = i;
            }
            // In all three cases, output b to the front side
            front_verts[num_front++] = b;
        }
        else if (b_side == PlaneClassification::Behind)
        {
            if (a_side == PlaneClassification::InFrontOf)
            {
                // Edge (a, b) straddles plane, output intersection point
                glm::vec2 i = {};
                F32 t = {};
                bool intersect = intersect_segment_plane(a, b, plane, t, i);
                Assert(intersect);
                assert(classify_point_on_plane(i, plane) == PlaneClassification::On);
                front_verts[num_front++] = back_verts[num_back++] = i;
            }
            else if (a_side == PlaneClassification::On)
            {
                // Output a when edge (a, b) goes from ‘on’ to ‘behind’ plane
                back_verts[num_back++] = a;
            }
            // In all three cases, output b to the back side
            back_verts[num_back++] = b;
        }
        else
        {
            // b is on the plane. In all three cases output b to the front side
            front_verts[num_front++] = b;
            // In one case, also output b to back side
            if (a_side == PlaneClassification::Behind)
                back_verts[num_back++] = b;
        }
        // Keep b as the starting point of the next edge
        a = b;
        a_side = b_side;
    }
    // Create (and return) two new polygons from the two vertex lists
    *front_poly = buffer_from_arr(arena, front_verts, num_front);
    *back_poly = buffer_from_arr(arena, back_verts, num_back);
}

Buffer<glm::vec2>
near_duplicate_vertices_discard_inplace(Buffer<glm::vec2> poly)
{
    F32 coordinate_scale = 0.0f;
    for (U32 i = 0; i < poly.size; i++)
    {
        coordinate_scale = (glm::max)(coordinate_scale, glm::abs(poly.data[i].x));
        coordinate_scale = (glm::max)(coordinate_scale, glm::abs(poly.data[i].y));
    }

    constexpr F32 absolute_tolerance = 0.001f;
    constexpr F32 relative_tolerance = 4.0f * std::numeric_limits<F32>::epsilon();
    F32 tolerance = (glm::max)(absolute_tolerance, relative_tolerance * coordinate_scale);
    F32 tolerance_squared = tolerance * tolerance;

    // Clipped polygons are convex, so any repeated position can be removed regardless
    // of whether the duplicate is adjacent or closes the polygon.
    U32 vertex_count = 0;
    for (U32 vertex_idx = 0; vertex_idx < poly.size; vertex_idx++)
    {
        glm::vec2 vertex = poly.data[vertex_idx];
        bool vertex_exists = false;
        for (U32 retained_vertex_idx = 0; retained_vertex_idx < vertex_count; ++retained_vertex_idx)
        {
            glm::vec2 vertex_delta = vertex - poly.data[retained_vertex_idx];
            F32 distance_squared = glm::dot(vertex_delta, vertex_delta);
            if (distance_squared <= tolerance_squared)
            {
                vertex_exists = true;
                break;
            }
        }

        if (vertex_exists)
        {
            continue;
        }

        poly.data[vertex_count++] = vertex;
    }

    Buffer<glm::vec2> final_poly = {.data = poly.data, .size = vertex_count};
    return final_poly;
}

render::PrimitiveMesh
hover_icon_mesh_create(Arena* arena, F32 radius, F32 height, F32 hover_height, glm::vec4 color)
{
    AssertAlways(arena);
    AssertAlways(radius > 0.0f);
    AssertAlways(height > 0.0f);

    constexpr U64 source_vertex_count = 6;
    constexpr U64 vertex_count = 24;
    constexpr U64 index_count = 24;

    render::PrimitiveMesh mesh = {};
    mesh.vertices = buffer_alloc<render::PrimitiveVertex>(arena, vertex_count);
    mesh.indices = buffer_alloc<U16>(arena, index_count);

    F32 middle_height = hover_height + height * 0.5f;
    F32 top_height = hover_height + height;
    glm::vec3 source_vertices[source_vertex_count] = {
        glm::vec3(0.0f, 0.0f, hover_height),
        glm::vec3(0.0f, 0.0f, top_height),
        glm::vec3(radius, 0.0f, middle_height),
        glm::vec3(0.0f, radius, middle_height),
        glm::vec3(-radius, 0.0f, middle_height),
        glm::vec3(0.0f, -radius, middle_height),
    };

    U16 source_indices[index_count] = {
        1, 2, 3,
        1, 3, 4,
        1, 4, 5,
        1, 5, 2,
        0, 3, 2,
        0, 4, 3,
        0, 5, 4,
        0, 2, 5,
    };

    constexpr U64 triangle_vertex_count = 3;
    for (U64 triangle_idx = 0; triangle_idx < index_count / triangle_vertex_count; ++triangle_idx)
    {
        U64 triangle_offset = triangle_idx * triangle_vertex_count;
        glm::vec3 position_a = source_vertices[source_indices[triangle_offset]];
        glm::vec3 position_b = source_vertices[source_indices[triangle_offset + 1]];
        glm::vec3 position_c = source_vertices[source_indices[triangle_offset + 2]];
        glm::vec3 normal_unnormalized = glm::cross(position_b - position_a, position_c - position_a);
        glm::vec3 normal = glm::normalize(normal_unnormalized);

        for (U64 triangle_vertex_idx = 0; triangle_vertex_idx < triangle_vertex_count; ++triangle_vertex_idx)
        {
            U64 vertex_idx = triangle_offset + triangle_vertex_idx;
            U16 source_vertex_idx = source_indices[vertex_idx];
            mesh.vertices.data[vertex_idx] = {.pos = source_vertices[source_vertex_idx], .normal = normal, .color = color};
            mesh.indices.data[vertex_idx] = (U16)vertex_idx;
        }
    }

    return mesh;
}

render::PrimitiveMesh
cylinder_mesh_create(Arena* arena, F32 radius, F32 height, U32 side_count, glm::vec4 color)
{
    AssertAlways(arena);
    AssertAlways(radius > 0.0f);
    AssertAlways(height > 0.0f);
    AssertAlways(side_count >= 3);
    AssertAlways(side_count <= 16383);

    U64 vertex_count = (U64)side_count * 4 + 2;
    U64 index_count = (U64)side_count * 12;
    render::PrimitiveMesh mesh = {};
    mesh.vertices = buffer_alloc<render::PrimitiveVertex>(arena, vertex_count);
    mesh.indices = buffer_alloc<U16>(arena, index_count);

    U32 bottom_center_idx = side_count * 2;
    U32 bottom_ring_start_idx = bottom_center_idx + 1;
    U32 top_center_idx = bottom_ring_start_idx + side_count;
    U32 top_ring_start_idx = top_center_idx + 1;

    glm::vec3 bottom_normal = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 top_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices.data[bottom_center_idx] = {.pos = glm::vec3(0.0f), .normal = bottom_normal, .color = color};
    mesh.vertices.data[top_center_idx] = {.pos = glm::vec3(0.0f, 0.0f, height), .normal = top_normal, .color = color};

    constexpr F32 full_rotation_radians = 6.28318530717958647692f;
    for (U32 side_idx = 0; side_idx < side_count; ++side_idx)
    {
        F32 angle = full_rotation_radians * (F32)side_idx / (F32)side_count;
        F32 x = radius * glm::cos(angle);
        F32 y = radius * glm::sin(angle);
        glm::vec3 radial_normal = glm::vec3(x / radius, y / radius, 0.0f);
        glm::vec3 bottom_position = glm::vec3(x, y, 0.0f);
        glm::vec3 top_position = glm::vec3(x, y, height);

        U32 side_bottom_idx = side_idx * 2;
        U32 side_top_idx = side_bottom_idx + 1;
        U32 bottom_cap_idx = bottom_ring_start_idx + side_idx;
        U32 top_cap_idx = top_ring_start_idx + side_idx;
        mesh.vertices.data[side_bottom_idx] = {.pos = bottom_position, .normal = radial_normal, .color = color};
        mesh.vertices.data[side_top_idx] = {.pos = top_position, .normal = radial_normal, .color = color};
        mesh.vertices.data[bottom_cap_idx] = {.pos = bottom_position, .normal = bottom_normal, .color = color};
        mesh.vertices.data[top_cap_idx] = {.pos = top_position, .normal = top_normal, .color = color};
    }

    U64 output_index_idx = 0;
    for (U32 side_idx = 0; side_idx < side_count; ++side_idx)
    {
        U32 next_side_idx = (side_idx + 1) % side_count;
        U16 side_bottom_idx = (U16)(side_idx * 2);
        U16 side_top_idx = (U16)(side_bottom_idx + 1);
        U16 next_side_bottom_idx = (U16)(next_side_idx * 2);
        U16 next_side_top_idx = (U16)(next_side_bottom_idx + 1);
        U16 bottom_cap_idx = (U16)(bottom_ring_start_idx + side_idx);
        U16 next_bottom_cap_idx = (U16)(bottom_ring_start_idx + next_side_idx);
        U16 top_cap_idx = (U16)(top_ring_start_idx + side_idx);
        U16 next_top_cap_idx = (U16)(top_ring_start_idx + next_side_idx);

        mesh.indices.data[output_index_idx++] = side_bottom_idx;
        mesh.indices.data[output_index_idx++] = next_side_bottom_idx;
        mesh.indices.data[output_index_idx++] = side_top_idx;
        mesh.indices.data[output_index_idx++] = side_top_idx;
        mesh.indices.data[output_index_idx++] = next_side_bottom_idx;
        mesh.indices.data[output_index_idx++] = next_side_top_idx;

        mesh.indices.data[output_index_idx++] = (U16)bottom_center_idx;
        mesh.indices.data[output_index_idx++] = next_bottom_cap_idx;
        mesh.indices.data[output_index_idx++] = bottom_cap_idx;

        mesh.indices.data[output_index_idx++] = (U16)top_center_idx;
        mesh.indices.data[output_index_idx++] = top_cap_idx;
        mesh.indices.data[output_index_idx++] = next_top_cap_idx;
    }
    Assert(output_index_idx == mesh.indices.size);

    return mesh;
}

PolygonMesh2d
polygon_triangulate(Arena* arena, Buffer<glm::vec2> poly)
{
    poly = near_duplicate_vertices_discard_inplace(poly);
    if (poly.size < 3)
    {
        return {};
    }

    CDT::Triangulation<F32> cdt;
    auto vertex_x_get = [](const glm::vec2& vertex) -> F32 { return vertex.x; };
    auto vertex_y_get = [](const glm::vec2& vertex) -> F32 { return vertex.y; };

    try
    {
        cdt.insertVertices(poly.begin(), poly.end(), vertex_x_get, vertex_y_get);
        cdt.eraseSuperTriangle();
    }
    catch (const CDT::IntersectingConstraintsError& error)
    {
        const CDT::Edge& edge_0 = error.e1();
        const CDT::Edge& edge_1 = error.e2();
        ERROR_LOG("CDT intersecting constraints (%llu, %llu) and (%llu, %llu): %s", (U64)edge_0.v1(), (U64)edge_0.v2(), (U64)edge_1.v1(), (U64)edge_1.v2(), error.what());
        return {};
    }
    catch (const CDT::DuplicateVertexError& error)
    {
        ERROR_LOG("CDT duplicate vertices %llu and %llu: %s", (U64)error.v1(), (U64)error.v2(), error.what());
        return {};
    }
    catch (const CDT::Error& error)
    {
        ERROR_LOG("CDT triangulation error: %s", error.what());
        return {};
    }

    PolygonMesh2d mesh = {};
    mesh.vertices = buffer_alloc<glm::vec2>(arena, cdt.vertices.size());
    for (CDT::VertInd vertex_idx = 0; vertex_idx < (CDT::VertInd)cdt.vertices.size(); ++vertex_idx)
    {
        const CDT::V2d<F32>& vertex = cdt.vertices[vertex_idx];
        mesh.vertices.data[vertex_idx] = glm::vec2(vertex.x, vertex.y);
    }

    mesh.indices = buffer_alloc<U32>(arena, cdt.triangles.size() * 3);
    U32 output_index_idx = 0;
    for (const CDT::Triangle& triangle : cdt.triangles)
    {
        for (CDT::VertInd vertex_idx : triangle.vertices)
        {
            mesh.indices.data[output_index_idx++] = (U32)vertex_idx;
        }
    }

    return mesh;
}

Buffer<ClassifiedTriangle2d>
triangle_partition_by_quads(Arena* arena, Triangle2d triangle, Buffer<Quad2d> clipping_quads)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(&arena, 1);
    // Planar subdivision can grow very quickly for coarse faces containing
    // many roads. Reuse fixed work buffers so intermediate clip results cannot
    // consume the entire worker scratch arena.
    constexpr U32 partition_chunk_capacity = 256;
    ChunkList<Triangle2d>* remaining_triangles = chunk_list_create<Triangle2d>(scratch.arena, partition_chunk_capacity);
    ChunkList<Triangle2d>* next_remaining_triangles = chunk_list_create<Triangle2d>(scratch.arena, partition_chunk_capacity);
    ChunkList<ClassifiedTriangle2d>* claimed_triangles = chunk_list_create<ClassifiedTriangle2d>(scratch.arena, partition_chunk_capacity);
    chunk_list_insert(scratch.arena, remaining_triangles, triangle);
    for (U32 quad_idx = 0; quad_idx < clipping_quads.size; ++quad_idx)
    {
        Quad2d& quad = clipping_quads.data[quad_idx];
        chunk_list_empty(next_remaining_triangles);
        for (U32 remaining_triangle_idx = 0; remaining_triangle_idx < remaining_triangles->total_count; ++remaining_triangle_idx)
        {
            Triangle2d& remaining_triangle = (*remaining_triangles)[remaining_triangle_idx];

            // Clip and CDT allocations only need to live for this input
            // triangle. Excluding the partition work arena selects the other
            // thread scratch arena and releases it at the end of the iteration.
            ScratchScope triangle_scratch = ScratchScope(&scratch.arena, 1);
            ClipResult clip_result = quad_to_triangle_clipping(triangle_scratch.arena, remaining_triangle, quad);
            if (clip_result.inner.size < 3)
            {
                chunk_list_insert(scratch.arena, next_remaining_triangles, remaining_triangle);
                continue;
            }

            PolygonMesh2d inner_mesh = polygon_triangulate(triangle_scratch.arena, clip_result.inner);
            if (inner_mesh.indices.size < 3)
            {
                chunk_list_insert(scratch.arena, next_remaining_triangles, remaining_triangle);
                continue;
            }

            // Triangulate every remaining polygon before committing the inner
            // region. If any triangulation fails, retain the input triangle so
            // the partition cannot silently lose area.
            constexpr U64 outer_triangle_chunk_capacity = 16;
            ChunkList<Triangle2d>* outer_triangles = chunk_list_create<Triangle2d>(triangle_scratch.arena, outer_triangle_chunk_capacity);
            bool outer_triangulation_succeeded = true;
            for (Buffer<glm::vec2> outer_polygon : clip_result.outer)
            {
                if (outer_polygon.size < 3)
                {
                    continue;
                }

                PolygonMesh2d outer_mesh = polygon_triangulate(triangle_scratch.arena, outer_polygon);
                if (outer_mesh.indices.size < 3)
                {
                    outer_triangulation_succeeded = false;
                    break;
                }

                Assert(outer_mesh.indices.size % 3 == 0);
                for (U32 index_idx = 0; index_idx < outer_mesh.indices.size; index_idx += 3)
                {
                    Triangle2d outer_triangle = {};
                    for (U32 triangle_vertex_idx = 0; triangle_vertex_idx < ArrayCount(outer_triangle.v); ++triangle_vertex_idx)
                    {
                        U32 vertex_idx = outer_mesh.indices.data[index_idx + triangle_vertex_idx];
                        outer_triangle.v[triangle_vertex_idx] = outer_mesh.vertices.data[vertex_idx];
                    }
                    chunk_list_insert(triangle_scratch.arena, outer_triangles, outer_triangle);
                }
            }

            if (!outer_triangulation_succeeded)
            {
                chunk_list_insert(scratch.arena, next_remaining_triangles, remaining_triangle);
                continue;
            }

            Assert(inner_mesh.indices.size % 3 == 0);
            for (U32 index_idx = 0; index_idx < inner_mesh.indices.size; index_idx += 3)
            {
                ClassifiedTriangle2d* claimed_triangle = chunk_list_get_next(scratch.arena, claimed_triangles);
                *claimed_triangle = {};
                claimed_triangle->region_idx = quad_idx + 1;
                for (U32 triangle_vertex_idx = 0; triangle_vertex_idx < ArrayCount(claimed_triangle->triangle.v); ++triangle_vertex_idx)
                {
                    U32 vertex_idx = inner_mesh.indices.data[index_idx + triangle_vertex_idx];
                    claimed_triangle->triangle.v[triangle_vertex_idx] = inner_mesh.vertices.data[vertex_idx];
                }
            }

            for (Triangle2d& outer_triangle : *outer_triangles)
            {
                chunk_list_insert(scratch.arena, next_remaining_triangles, outer_triangle);
            }
        }

        Swap(ChunkList<Triangle2d>*, remaining_triangles, next_remaining_triangles);
        if (remaining_triangles->total_count == 0)
        {
            break;
        }
    }

    Buffer<ClassifiedTriangle2d> result = buffer_alloc<ClassifiedTriangle2d>(arena, claimed_triangles->total_count + remaining_triangles->total_count);
    buffer_from_chunk_list_append(result, 0, claimed_triangles);

    for (U32 remaining_triangle_idx = 0; remaining_triangle_idx < remaining_triangles->total_count; ++remaining_triangle_idx)
    {
        ClassifiedTriangle2d* terrain_triangle = &result.data[claimed_triangles->total_count + remaining_triangle_idx];
        terrain_triangle->triangle = (*remaining_triangles)[remaining_triangle_idx];
    }
    return result;
}

// NOTE: Both clip triangle and subject polygon should have counter clockwise winding
ClipResult
subject_to_triangle_clipping(Arena* arena, Triangle2d& clip_triangle, glm::vec2* subject_poly, U32 subject_poly_side_count)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    // Clip the subject polygon against each counter-clockwise clipping edge.
    ChunkList<glm::vec2>* back_poly_chunk_list = chunk_list_create<glm::vec2>(scratch.arena, 10);
    Buffer<glm::vec2> final_front_poly = buffer_from_arr(scratch.arena, clip_triangle.v, ArrayCount(clip_triangle.v));
    final_front_poly = near_duplicate_vertices_discard_inplace(final_front_poly);
    if (final_front_poly.size < 3)
    {
        return {};
    }

    for (U32 sub_idx = 0; sub_idx < subject_poly_side_count; ++sub_idx)
    {
        glm::vec2 p0 = subject_poly[sub_idx];
        glm::vec2 p1 = subject_poly[(sub_idx + 1) % subject_poly_side_count];

        glm::vec2 edge_direction = p1 - p0;
        F32 edge_length_squared = glm::dot(edge_direction, edge_direction);
        if (edge_length_squared == 0.0f)
        {
            continue;
        }

        geometry::Plane2d line = line_from_points(p0, p1);

        PolygonPlaneClassification classification = classify_polygon_to_plane(final_front_poly.data, final_front_poly.size, line);

        if (classification == PolygonPlaneClassification::InFrontOf || classification == PolygonPlaneClassification::CoPlanar)
        {
            continue;
        }

        if (classification == PolygonPlaneClassification::Behind)
        {
            if (final_front_poly.size >= 3)
            {
                ChunkItem<glm::vec2>* back_poly_chunk = chunk_item_from_array(scratch.arena, final_front_poly.data, final_front_poly.size);
                chunk_list_insert_chunk(back_poly_chunk_list, back_poly_chunk);
            }

            final_front_poly = {};
            break;
        }

        Buffer<glm::vec2> front_poly = {};
        Buffer<glm::vec2> back_poly = {};
        split_polygon(scratch.arena, final_front_poly.data, final_front_poly.size, line, &front_poly, &back_poly);

        back_poly = near_duplicate_vertices_discard_inplace(back_poly);
        if (back_poly.size >= 3)
        {
            ChunkItem<glm::vec2>* back_poly_chunk = chunk_item_from_array(scratch.arena, back_poly.data, back_poly.size);
            chunk_list_insert_chunk(back_poly_chunk_list, back_poly_chunk);
        }

        front_poly = near_duplicate_vertices_discard_inplace(front_poly);
        if (front_poly.size < 3)
        {
            final_front_poly = {};
            break;
        }

        final_front_poly = front_poly;
    }

    // find the number of quad edges intersecting the triangle. (to determine number of convex polygons outside road)
    // split along each intersecting edge with each inner polygon going through to next intersection test
    // this is to be done for all road quads crossing the triangle
    // The edges of the inner triangle can now be added as constraints to a constrained delauney triangulation
    // the outside polygon edges can also be added as constraints
    // Remove duplicate edges edges and vertices from both inner and outer polygons
    // TODO: What to do about sliver and T-junctions afterwards
    // NOTE: Two adjacent triangles intersected by same edge might share the same vertex and intersection might change based on edge_start and edge_end direction

    Buffer<Buffer<glm::vec2>> back_poly_buffer = buffer_alloc<Buffer<glm::vec2>>(arena, back_poly_chunk_list->chunk_count);
    U32 i = 0;
    for (ChunkItem<glm::vec2>* chunk = back_poly_chunk_list->first; chunk; chunk = chunk->next, ++i)
    {
        back_poly_buffer.data[i] = buffer_from_arr(arena, chunk->values, chunk->count);
    }

    ClipResult result = {};
    result.inner = buffer_arena_copy(arena, final_front_poly);
    result.outer = back_poly_buffer;
    return result;
}

ClipResult
quad_to_triangle_clipping(Arena* arena, Triangle2d& triangle, Quad2d& quad)
{
    ClipResult result = subject_to_triangle_clipping(arena, triangle, quad.v, ArrayCount(quad.v));
    return result;
}

} // namespace geometry
