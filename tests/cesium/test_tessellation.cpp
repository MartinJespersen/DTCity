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

        geometry::PolygonMesh2d mesh = geometry::polygon_triangulate(arena, polygon);

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
