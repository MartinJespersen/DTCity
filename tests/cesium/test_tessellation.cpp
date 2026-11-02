#include "../test_inc.hpp"
#include "city/tessellation.hpp"

TEST_CASE("tile tessellation preserves shared surface vertices without road classification")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    render::TileVertex vertices[] = {
        {.pos = {0, 0, 0}}, {.pos = {10, 0, 0}}, {.pos = {10, 10, 0}}, {.pos = {0, 10, 0}},
    };
    U32 indices[] = {0, 1, 2, 0, 2, 3};
    city::RoadBvh bvh = {};
    city::RoadSegmentNode root = {};
    city::RoadSegmentCorners roads[101] = {};
    U32 index_count = ArrayCount(indices);

    SUBCASE("no road network") {}
    SUBCASE("empty tile") { index_count = 0; }
    SUBCASE("road outside the tile")
    {
        root.bounds = {.min = {20, 20}, .max = {30, 30}};
        root.end_idx = 1;
        roads[0].corners[0] = {20, 20};
        roads[0].corners[1] = {30, 20};
        roads[0].corners[2] = {30, 30};
        roads[0].corners[3] = {20, 30};
        bvh.root = &root;
        bvh.road_segment_buffer_sorted = {roads, 1};
    }
    SUBCASE("candidate limit leaves dense faces intact")
    {
        root.bounds = {.min = {-1, -1}, .max = {11, 11}};
        root.end_idx = ArrayCount(roads);
        for (city::RoadSegmentCorners& road : roads)
        {
            road.corners[0] = {-1, -1};
            road.corners[1] = {11, -1};
            road.corners[2] = {11, 11};
            road.corners[3] = {-1, 11};
        }
        bvh.root = &root;
        bvh.road_segment_buffer_sorted = {roads, ArrayCount(roads)};
    }

    render::TileMeshParts parts = {};
    CHECK_FALSE(city::tesselate_roads(arena, {vertices, ArrayCount(vertices)}, {indices, index_count}, bvh, &parts));
    render::TileMesh& surface = parts.meshes[enum_idx(render::TileMeshKind::Surface)];
    render::TileMesh& road = parts.meshes[enum_idx(render::TileMeshKind::Road)];
    REQUIRE(surface.indices.size == index_count);
    CHECK(surface.vertices.size == (index_count ? ArrayCount(vertices) : 0));
    CHECK(road.indices.size == 0);
    CHECK(road.vertices.size == 0);
    for (U32 index = 0; index < index_count; ++index)
    {
        REQUIRE(surface.indices.data[index] < surface.vertices.size);
        const render::TileVertex& vertex = surface.vertices.data[surface.indices.data[index]];
        CHECK(vertex.pos == vertices[indices[index]].pos);
        CHECK(vertex.road_segment_idx == 0);
    }
}

TEST_CASE("tile tessellation separates road regions and preserves winding and vertex attributes")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    render::TileVertex vertices[] = {
        {.pos = {0, 0, 0}, .uv = {0, 0}, .overlay_uv = {1, 2}},
        {.pos = {10, 0, 10}, .uv = {1, 0}, .overlay_uv = {3, 2}},
        {.pos = {0, 10, 20}, .uv = {0, 1}, .overlay_uv = {1, 5}},
    };
    U32 indices[] = {0, 1, 2};
    city::RoadSegmentCorners roads[2] = {};
    roads[0].corners[0] = {2, -1};
    roads[0].corners[1] = {4, -1};
    roads[0].corners[2] = {4, 11};
    roads[0].corners[3] = {2, 11};
    city::RoadSegmentNode root = {.bounds = {.min = {-1, -1}, .max = {11, 11}}, .end_idx = 1};
    F64 expected_road_area = 14.0;
    F64 winding = 1.0;

    SUBCASE("partial road") {}
    SUBCASE("clockwise source triangle")
    {
        indices[1] = 2;
        indices[2] = 1;
        winding = -1.0;
    }
    SUBCASE("clockwise road corners")
    {
        Vec2F32 corner = roads[0].corners[1];
        roads[0].corners[1] = roads[0].corners[3];
        roads[0].corners[3] = corner;
    }
    SUBCASE("whole face is road")
    {
        roads[0].corners[0] = {-1, -1};
        roads[0].corners[1] = {11, -1};
        roads[0].corners[2] = {11, 11};
        roads[0].corners[3] = {-1, 11};
        expected_road_area = 50.0;
    }
    SUBCASE("two roads retain separate segment indices")
    {
        roads[1] = roads[0];
        for (Vec2F32& corner : roads[1].corners)
            corner.x += 4;
        root.end_idx = 2;
        expected_road_area = 20.0;
    }

    city::RoadBvh bvh = {.root = &root, .road_segment_buffer_sorted = {roads, root.end_idx}};
    render::TileMeshParts parts = {};
    REQUIRE(city::tesselate_roads(arena, {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)}, bvh, &parts));
    F64 areas[enum_idx(render::TileMeshKind::Count)] = {};
    B32 found_road[2] = {};
    for (U32 part = 0; part < ArrayCount(parts.meshes); ++part)
    {
        render::TileMesh& mesh = parts.meshes[part];
        REQUIRE(mesh.indices.size % 3 == 0);
        for (U32 index : mesh.indices)
            REQUIRE(index < mesh.vertices.size);
        for (U32 index = 0; index < mesh.indices.size; index += 3)
        {
            const render::TileVertex& a = mesh.vertices.data[mesh.indices.data[index]];
            const render::TileVertex& b = mesh.vertices.data[mesh.indices.data[index + 1]];
            const render::TileVertex& c = mesh.vertices.data[mesh.indices.data[index + 2]];
            glm::dvec2 ab = glm::dvec2(b.pos) - glm::dvec2(a.pos);
            glm::dvec2 ac = glm::dvec2(c.pos) - glm::dvec2(a.pos);
            F64 twice_area = ab.x * ac.y - ab.y * ac.x;
            CHECK(twice_area * winding > 0);
            areas[part] += glm::abs(twice_area) * 0.5;
            CHECK(a.road_segment_idx == b.road_segment_idx);
            CHECK(a.road_segment_idx == c.road_segment_idx);
        }
        for (const render::TileVertex& vertex : mesh.vertices)
        {
            CHECK(vertex.pos.z == doctest::Approx(vertex.pos.x + 2 * vertex.pos.y));
            CHECK(vertex.uv.x == doctest::Approx(vertex.pos.x * 0.1f));
            CHECK(vertex.uv.y == doctest::Approx(vertex.pos.y * 0.1f));
            CHECK(vertex.overlay_uv.x == doctest::Approx(1 + vertex.pos.x * 0.2f));
            CHECK(vertex.overlay_uv.y == doctest::Approx(2 + vertex.pos.y * 0.3f));
            if (part == enum_idx(render::TileMeshKind::Surface))
                CHECK(vertex.road_segment_idx == 0);
            else
            {
                REQUIRE(vertex.road_segment_idx >= 1);
                REQUIRE(vertex.road_segment_idx <= root.end_idx);
                found_road[vertex.road_segment_idx - 1] = true;
                if (expected_road_area != 50.0)
                {
                    F32 road_min_x = vertex.road_segment_idx == 1 ? 2.0f : 6.0f;
                    CHECK(vertex.pos.x >= road_min_x - 0.0001f);
                    CHECK(vertex.pos.x <= road_min_x + 2.0001f);
                }
            }
        }
    }
    CHECK(areas[enum_idx(render::TileMeshKind::Road)] == doctest::Approx(expected_road_area));
    CHECK(areas[enum_idx(render::TileMeshKind::Surface)] == doctest::Approx(50.0 - expected_road_area));
    CHECK(found_road[0]);
    CHECK(found_road[1] == (root.end_idx == 2));
}

TEST_CASE("tile tessellation offsets each mesh across split and unchanged faces")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    render::TileVertex vertices[] = {
        {.pos = {0, 0, 0}}, {.pos = {10, 0, 0}}, {.pos = {10, 10, 0}}, {.pos = {0, 10, 0}},
        {.pos = {20, 0, 0}}, {.pos = {30, 0, 0}}, {.pos = {30, 10, 0}}, {.pos = {20, 10, 0}},
    };
    U32 indices[] = {4, 5, 6, 0, 1, 2, 0, 2, 3, 4, 6, 7};
    city::RoadSegmentCorners road = {};
    road.corners[0] = {2, -1};
    road.corners[1] = {4, -1};
    road.corners[2] = {4, 11};
    road.corners[3] = {2, 11};
    city::RoadSegmentNode root = {.bounds = {.min = {2, -1}, .max = {4, 11}}, .end_idx = 1};
    city::RoadBvh bvh = {.root = &root, .road_segment_buffer_sorted = {&road, 1}};
    render::TileMeshParts parts = {};
    REQUIRE(city::tesselate_roads(arena, {vertices, ArrayCount(vertices)}, {indices, ArrayCount(indices)}, bvh, &parts));
    for (U32 part = 0; part < ArrayCount(parts.meshes); ++part)
    {
        render::TileMesh& mesh = parts.meshes[part];
        F64 area = 0;
        U32 unchanged_vertices = 0;
        for (U32 index : mesh.indices)
            REQUIRE(index < mesh.vertices.size);
        for (const render::TileVertex& vertex : mesh.vertices)
        {
            CHECK(vertex.road_segment_idx == (part == enum_idx(render::TileMeshKind::Road) ? 1 : 0));
            unchanged_vertices += vertex.pos.x >= 20;
        }
        for (U32 index = 0; index < mesh.indices.size; index += 3)
        {
            glm::dvec2 a = mesh.vertices.data[mesh.indices.data[index]].pos;
            glm::dvec2 b = mesh.vertices.data[mesh.indices.data[index + 1]].pos;
            glm::dvec2 c = mesh.vertices.data[mesh.indices.data[index + 2]].pos;
            area += ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5;
        }
        CHECK(area == doctest::Approx(part == enum_idx(render::TileMeshKind::Road) ? 20.0 : 180.0));
        CHECK(unchanged_vertices == (part == enum_idx(render::TileMeshKind::Road) ? 0 : 4));
    }
}

TEST_CASE("segment-plane intersection remains on plane for large coordinates")
{
    glm::vec2 a = {-4595512.5f, -4600893.5f};
    glm::vec2 b = {32437188.0f, 32476254.0f};
    geometry::Plane2d plane = {
        .n = {-0.7066825628f, -0.7075307369f},
        .d = 30.2661018372f,
    };

    F32 t = 0.0f;
    glm::vec2 intersection = {};
    REQUIRE(geometry::intersect_segment_plane(a, b, plane, t, intersection));
    CHECK(geometry::classify_point_on_plane(intersection, plane) == geometry::PlaneClassification::On);
}

TEST_CASE("triangle clipping returns a valid polygon partition")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    auto polygon_twice_area = [](Buffer<glm::vec2> polygon) -> F64
    {
        F64 twice_area = 0.0;
        for (U64 vertex_idx = 0; vertex_idx < polygon.size; ++vertex_idx)
        {
            const glm::vec2& a = polygon.data[vertex_idx];
            const glm::vec2& b = polygon.data[(vertex_idx + 1) % polygon.size];
            twice_area += (F64)a.x * (F64)b.y - (F64)a.y * (F64)b.x;
        }
        return twice_area;
    };

    auto check_polygon = [&](Buffer<glm::vec2> polygon)
    {
        REQUIRE(polygon.size >= 3);

        for (U64 vertex_idx = 0; vertex_idx < polygon.size; ++vertex_idx)
        {
            const glm::vec2& a = polygon.data[vertex_idx];
            const glm::vec2& b = polygon.data[(vertex_idx + 1) % polygon.size];
            glm::vec2 edge = b - a;
            F32 edge_length_squared = glm::dot(edge, edge);
            CHECK(edge_length_squared > 0.0f);
        }

        F64 twice_area = polygon_twice_area(polygon);
        CHECK(twice_area > 0.0);
    };

    auto check_partition = [&](geometry::Triangle2d& triangle, glm::vec2* subject_poly, U32 subject_poly_count)
    {
        geometry::ClipResult result = geometry::subject_to_triangle_clipping(arena, triangle, subject_poly, subject_poly_count);

        F64 partition_twice_area = 0.0;
        if (result.inner.size)
        {
            check_polygon(result.inner);
            partition_twice_area += polygon_twice_area(result.inner);
        }

        for (Buffer<glm::vec2> outer_polygon : result.outer)
        {
            check_polygon(outer_polygon);
            partition_twice_area += polygon_twice_area(outer_polygon);
        }

        Buffer<glm::vec2> triangle_polygon = {
            .data = triangle.v,
            .size = ArrayCount(triangle.v),
        };
        F64 triangle_twice_area = polygon_twice_area(triangle_polygon);
        F64 area_tolerance = (glm::max)(0.000001, glm::abs(triangle_twice_area) * 0.000001);
        CHECK(glm::abs(partition_twice_area - triangle_twice_area) <= area_tolerance);

        return result;
    };

    SUBCASE("road crosses a face")
    {
        geometry::Triangle2d triangle = {{
            {0.0f, 0.0f},
            {20.0f, 0.0f},
            {0.0f, 20.0f},
        }};
        glm::vec2 road[] = {
            {2.0f, -2.0f},
            {6.0f, -2.0f},
            {6.0f, 8.0f},
            {2.0f, 8.0f},
        };

        geometry::ClipResult result = check_partition(triangle, road, ArrayCount(road));
        REQUIRE(result.inner.size >= 3);

        F64 inner_twice_area = polygon_twice_area(result.inner);
        CHECK(inner_twice_area == doctest::Approx(64.0));
    }

    SUBCASE("road 20511 near-degenerate inner sliver is discarded")
    {
        geometry::Triangle2d triangle = {{
            {2292.13477f, -560.493042f},
            {2292.08423f, -473.990936f},
            {2292.13062f, -560.492004f},
        }};
        glm::vec2 road[] = {
            {2321.96558f, -487.30127f},
            {2279.34937f, -475.152863f},
            {2278.87329f, -476.824554f},
            {2321.4895f, -488.972961f},
        };

        geometry::ClipResult result = geometry::subject_to_triangle_clipping(arena, triangle, road, ArrayCount(road));
        CHECK(result.inner.size == 0);
    }
}

TEST_CASE("road 19022 is rejected as a convex clipping input")
{
    glm::vec2 road[] = {
        {-191.972748f, 436.648438f},
        {-192.433777f, 438.184875f},
        {-193.817368f, 436.144867f},
        {-193.890335f, 436.159485f},
    };

    bool has_clockwise_turn = false;
    bool has_counter_clockwise_turn = false;
    for (U32 vertex_idx = 0; vertex_idx < ArrayCount(road); ++vertex_idx)
    {
        glm::vec2 a = road[vertex_idx];
        glm::vec2 b = road[(vertex_idx + 1) % ArrayCount(road)];
        glm::vec2 c = road[(vertex_idx + 2) % ArrayCount(road)];
        glm::vec2 ab = b - a;
        glm::vec2 bc = c - b;
        F32 turn = ab.x * bc.y - ab.y * bc.x;
        has_clockwise_turn |= turn < 0.0f;
        has_counter_clockwise_turn |= turn > 0.0f;
    }

    CHECK(has_clockwise_turn);
    CHECK(has_counter_clockwise_turn);
}

TEST_CASE("quad to triangle clipping returns the overlapping inner polygon and outer polygons")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    geometry::Triangle2d triangle = {{
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {0.0f, 10.0f},
    }};
    geometry::Quad2d quad = {{
        {2.0f, -2.0f},
        {6.0f, -2.0f},
        {6.0f, 4.0f},
        {2.0f, 4.0f},
    }};

    geometry::ClipResult result = geometry::quad_to_triangle_clipping(arena, triangle, quad);

    REQUIRE(result.inner.size == 4);

    F64 inner_twice_area = 0.0;
    for (U64 vertex_idx = 0; vertex_idx < result.inner.size; ++vertex_idx)
    {
        const glm::vec2& a = result.inner.data[vertex_idx];
        const glm::vec2& b = result.inner.data[(vertex_idx + 1) % result.inner.size];
        inner_twice_area += (F64)a.x * (F64)b.y - (F64)a.y * (F64)b.x;
    }

    CHECK(inner_twice_area == doctest::Approx(32.0));
    CHECK(result.outer.size == 3);
    U32 total_outer_vertex_count = 0;
    for (auto outer : result.outer)
    {
        total_outer_vertex_count += outer.size;
    }
    CHECK(total_outer_vertex_count == 10);
    CHECK(result.outer.size > 0);
}

TEST_CASE("quad to triangle clipping 2 returns the overlapping inner polygon and outer polygons")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    geometry::Triangle2d triangle = {{
        {-2.0f, 0.0f},
        {2.0f, 0.0f},
        {0.0f, 2.0f},
    }};
    geometry::Quad2d quad = {{
        {-2.0f, 1.5f},
        {-2.0f, 0.5f},
        {2.0f, 0.5f},
        {2.0f, 1.5f},
    }};

    geometry::ClipResult result = geometry::quad_to_triangle_clipping(arena, triangle, quad);

    REQUIRE(result.inner.size == 4);

    CHECK(result.outer.size == 2);
    U32 total_outer_vertex_count = 0;
    for (auto outer : result.outer)
    {
        total_outer_vertex_count += outer.size;
    }
    CHECK(total_outer_vertex_count == 7);
}

TEST_CASE("polygon cleanup removes closing and non-adjacent duplicate vertices")
{
    glm::vec2 vertices[] = {
        {0.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, 0.0f}, {2.0f, 2.0f}, {0.0f, 2.0f}, {0.0f, 0.0f},
    };
    Buffer<glm::vec2> polygon = {
        .data = vertices,
        .size = ArrayCount(vertices),
    };

    polygon = geometry::near_duplicate_vertices_discard_inplace(polygon);

    REQUIRE(polygon.size == 4);
    CHECK(polygon.data[0] == glm::vec2(0.0f, 0.0f));
    CHECK(polygon.data[1] == glm::vec2(2.0f, 0.0f));
    CHECK(polygon.data[2] == glm::vec2(2.0f, 2.0f));
    CHECK(polygon.data[3] == glm::vec2(0.0f, 2.0f));
}

TEST_CASE("inner and outer clipping polygons can be tessellated")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    auto check_polygon_tessellation = [&](Buffer<glm::vec2> polygon)
    {
        REQUIRE(polygon.size >= 3);
        REQUIRE(polygon.size <= 4);

        geometry::PolygonMesh2d mesh = geometry::convex_polygon_triangulate(arena, polygon);

        REQUIRE(mesh.vertices.size == polygon.size);
        REQUIRE(mesh.indices.size == (polygon.size - 2) * 3);

        F64 triangulated_twice_area = 0.0;
        for (U64 index_idx = 0; index_idx < mesh.indices.size; index_idx += 3)
        {
            U32 index_0 = mesh.indices.data[index_idx];
            U32 index_1 = mesh.indices.data[index_idx + 1];
            U32 index_2 = mesh.indices.data[index_idx + 2];
            REQUIRE(index_0 < mesh.vertices.size);
            REQUIRE(index_1 < mesh.vertices.size);
            REQUIRE(index_2 < mesh.vertices.size);

            glm::dvec2 a = mesh.vertices.data[index_0];
            glm::dvec2 b = mesh.vertices.data[index_1];
            glm::dvec2 c = mesh.vertices.data[index_2];
            glm::dvec2 ab = b - a;
            glm::dvec2 ac = c - a;
            F64 triangle_twice_area = ab.x * ac.y - ab.y * ac.x;
            triangulated_twice_area += glm::abs(triangle_twice_area);
        }

        F64 polygon_twice_area = 0.0;
        for (U64 vertex_idx = 0; vertex_idx < polygon.size; ++vertex_idx)
        {
            glm::dvec2 a = polygon.data[vertex_idx];
            glm::dvec2 b = polygon.data[(vertex_idx + 1) % polygon.size];
            polygon_twice_area += a.x * b.y - a.y * b.x;
        }

        CHECK(triangulated_twice_area == doctest::Approx(glm::abs(polygon_twice_area)).epsilon(0.000001));
    };

    geometry::Triangle2d triangle = {{
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {0.0f, 10.0f},
    }};
    geometry::Quad2d quad = {{
        {2.0f, -2.0f},
        {6.0f, -2.0f},
        {6.0f, 4.0f},
        {2.0f, 4.0f},
    }};
    geometry::ClipResult clip_result = geometry::quad_to_triangle_clipping(arena, triangle, quad);

    REQUIRE(clip_result.inner.size == 4);
    check_polygon_tessellation(clip_result.inner);

    REQUIRE(clip_result.outer.size == 3);
    U64 outer_polygon_idx = 0;
    for (Buffer<glm::vec2> outer_polygon : clip_result.outer)
    {
        CAPTURE(outer_polygon_idx);
        check_polygon_tessellation(outer_polygon);
        outer_polygon_idx++;
    }
}

TEST_CASE("convex polygon triangulation preserves area and emits counter-clockwise triangles")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    glm::vec2 vertices[] = {{0, 0}, {2, 0}, {4, 1}, {5, 3}, {4, 5}, {2, 6}, {0, 6}, {-2, 5}, {-3, 3}, {-2, 1}};
    for (U64 count = 3; count <= ArrayCount(vertices); ++count)
    {
        for (U32 reverse = 0; reverse < 2; ++reverse)
        {
            CAPTURE(count);
            CAPTURE(reverse);
            glm::vec2 ordered[ArrayCount(vertices)] = {};
            F64 polygon_area = 0.0;
            for (U64 i = 0; i < count; ++i)
            {
                ordered[i] = vertices[reverse ? count - 1 - i : i];
            }
            for (U64 i = 0; i < count; ++i)
            {
                glm::dvec2 a = ordered[i];
                glm::dvec2 b = ordered[(i + 1) % count];
                polygon_area += a.x * b.y - a.y * b.x;
            }
            Buffer<glm::vec2> polygon = {.data = ordered, .size = count};
            geometry::PolygonMesh2d mesh = geometry::convex_polygon_triangulate(arena, polygon);
            REQUIRE(mesh.indices.size == (count - 2) * 3);
            F64 triangle_area = 0.0;
            for (U64 i = 0; i < mesh.indices.size; i += 3)
            {
                REQUIRE(mesh.indices.data[i] < count);
                REQUIRE(mesh.indices.data[i + 1] < count);
                REQUIRE(mesh.indices.data[i + 2] < count);
                glm::dvec2 a = mesh.vertices.data[mesh.indices.data[i]];
                glm::dvec2 b = mesh.vertices.data[mesh.indices.data[i + 1]];
                glm::dvec2 c = mesh.vertices.data[mesh.indices.data[i + 2]];
                glm::dvec2 ab = b - a;
                glm::dvec2 ac = c - a;
                F64 area = ab.x * ac.y - ab.y * ac.x;
                CHECK(area > 0.0);
                triangle_area += area;
            }
            CHECK(triangle_area == doctest::Approx(glm::abs(polygon_area)));
        }
    }

    glm::vec2 collinear[] = {{0, 0}, {1, 0}, {2, 0}};
    Buffer<glm::vec2> line = {.data = collinear, .size = ArrayCount(collinear)};
    geometry::PolygonMesh2d empty = geometry::convex_polygon_triangulate(arena, line);
    CHECK(empty.indices.size == 0);
}

TEST_CASE("convex quad triangulation chooses the diagonal with better worst triangle quality")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    glm::vec2 vertices[] = {{0, 0}, {4, 0}, {4, 4}, {0, 1}};
    // The 1--3 diagonal has minimum quality 4/34; 0--2 has 4/58.
    // Rotate and reverse the input to exercise both index diagonals and winding.
    for (U32 offset = 0; offset < 4; ++offset)
    {
        for (U32 reverse = 0; reverse < 2; ++reverse)
        {
            glm::vec2 ordered[4] = {};
            for (U32 i = 0; i < 4; ++i)
            {
                U32 source = (offset + (reverse ? 4 - i : i)) % 4;
                ordered[i] = vertices[source];
            }
            Buffer<glm::vec2> polygon = {.data = ordered, .size = 4};
            geometry::PolygonMesh2d mesh = geometry::convex_polygon_triangulate(arena, polygon);
            REQUIRE(mesh.indices.size == 6);
            F64 worst_quality = 1.0;
            for (U32 i = 0; i < 6; i += 3)
            {
                glm::dvec2 a = mesh.vertices.data[mesh.indices.data[i]];
                glm::dvec2 b = mesh.vertices.data[mesh.indices.data[i + 1]];
                glm::dvec2 c = mesh.vertices.data[mesh.indices.data[i + 2]];
                glm::dvec2 ab = b - a;
                glm::dvec2 ac = c - a;
                glm::dvec2 bc = c - b;
                F64 twice_area = ab.x * ac.y - ab.y * ac.x;
                F64 ab_squared = glm::dot(ab, ab);
                F64 ac_squared = glm::dot(ac, ac);
                F64 bc_squared = glm::dot(bc, bc);
                F64 quality = twice_area / (ab_squared + ac_squared + bc_squared);
                worst_quality = Min(worst_quality, quality);
            }
            CHECK(worst_quality == doctest::Approx(4.0 / 34.0));
        }
    }
}

TEST_CASE("quad clipping can produce a pentagon")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    geometry::Triangle2d triangle = {{{0, 0}, {10, 0}, {0, 10}}};
    geometry::Quad2d quad = {{{2, -1}, {8, -1}, {8, 6}, {2, 6}}};
    geometry::ClipResult clip = geometry::quad_to_triangle_clipping(arena, triangle, quad);
    REQUIRE(clip.inner.size == 5);
    geometry::PolygonMesh2d mesh = geometry::convex_polygon_triangulate(arena, clip.inner);
    CHECK(mesh.indices.size == 9);
}

TEST_CASE("multiple road quads partition a coarse triangle without losing area")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    geometry::Triangle2d triangle = {{
        {0.0f, 0.0f},
        {20.0f, 0.0f},
        {0.0f, 20.0f},
    }};
    geometry::Quad2d quads[] = {
        {{
            {2.0f, -1.0f},
            {4.0f, -1.0f},
            {4.0f, 15.0f},
            {2.0f, 15.0f},
        }},
        {{
            {8.0f, -1.0f},
            {10.0f, -1.0f},
            {10.0f, 15.0f},
            {8.0f, 15.0f},
        }},
    };
    Buffer<geometry::Quad2d> quad_buffer = {
        .data = quads,
        .size = ArrayCount(quads),
    };

    Buffer<geometry::ClassifiedTriangle2d> partition = geometry::triangle_partition_by_quads(arena, triangle, quad_buffer);
    REQUIRE(partition.size > 0);

    bool found_terrain = false;
    bool found_first_road = false;
    bool found_second_road = false;
    F64 partition_twice_area = 0.0;
    for (geometry::ClassifiedTriangle2d& classified_triangle : partition)
    {
        glm::dvec2 edge_0 = glm::dvec2(classified_triangle.triangle.v[1]) - glm::dvec2(classified_triangle.triangle.v[0]);
        glm::dvec2 edge_1 = glm::dvec2(classified_triangle.triangle.v[2]) - glm::dvec2(classified_triangle.triangle.v[0]);
        F64 triangle_twice_area = edge_0.x * edge_1.y - edge_0.y * edge_1.x;
        CHECK(glm::abs(triangle_twice_area) > 0.000001);
        partition_twice_area += glm::abs(triangle_twice_area);

        found_terrain |= classified_triangle.region_idx == 0;
        found_first_road |= classified_triangle.region_idx == 1;
        found_second_road |= classified_triangle.region_idx == 2;
    }

    glm::dvec2 original_edge_0 = glm::dvec2(triangle.v[1]) - glm::dvec2(triangle.v[0]);
    glm::dvec2 original_edge_1 = glm::dvec2(triangle.v[2]) - glm::dvec2(triangle.v[0]);
    F64 original_twice_area = original_edge_0.x * original_edge_1.y - original_edge_0.y * original_edge_1.x;

    CHECK(found_terrain);
    CHECK(found_first_road);
    CHECK(found_second_road);
    CHECK(partition_twice_area == doctest::Approx(glm::abs(original_twice_area)).epsilon(0.000001));
}
