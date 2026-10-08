#include "../test_inc.hpp"

TEST_CASE("hover icon mesh is an outward-facing diamond above its origin")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    F32 radius = 2.0f;
    F32 height = 6.0f;
    F32 hover_height = 3.0f;
    glm::vec4 color = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    render::PrimitiveMesh mesh = geometry::hover_icon_mesh_create(arena, radius, height, hover_height, color);

    REQUIRE(mesh.vertices.size == 24);
    REQUIRE(mesh.indices.size == 24);

    F32 min_height = max_f32;
    F32 max_height = -max_f32;
    for (U64 vertex_idx = 0; vertex_idx < mesh.vertices.size; ++vertex_idx)
    {
        render::PrimitiveVertex& vertex = mesh.vertices.data[vertex_idx];
        min_height = Min(min_height, vertex.pos.z);
        max_height = Max(max_height, vertex.pos.z);
        CHECK(vertex.pos.z >= hover_height);
        CHECK(vertex.pos.z <= hover_height + height);
        CHECK(glm::length(vertex.normal) == doctest::Approx(1.0f));
        CHECK(vertex.color.x == doctest::Approx(color.x));
        CHECK(vertex.color.y == doctest::Approx(color.y));
        CHECK(vertex.color.z == doctest::Approx(color.z));
        CHECK(vertex.color.w == doctest::Approx(color.w));
    }
    CHECK(min_height == doctest::Approx(hover_height));
    CHECK(max_height == doctest::Approx(hover_height + height));

    glm::vec3 mesh_center = glm::vec3(0.0f, 0.0f, hover_height + height * 0.5f);
    for (U64 index_idx = 0; index_idx < mesh.indices.size; index_idx += 3)
    {
        U16 index_a = mesh.indices.data[index_idx];
        U16 index_b = mesh.indices.data[index_idx + 1];
        U16 index_c = mesh.indices.data[index_idx + 2];
        REQUIRE(index_a < mesh.vertices.size);
        REQUIRE(index_b < mesh.vertices.size);
        REQUIRE(index_c < mesh.vertices.size);

        glm::vec3 position_a = mesh.vertices.data[index_a].pos;
        glm::vec3 position_b = mesh.vertices.data[index_b].pos;
        glm::vec3 position_c = mesh.vertices.data[index_c].pos;
        glm::vec3 face_normal_unnormalized = glm::cross(position_b - position_a, position_c - position_a);
        glm::vec3 face_normal = glm::normalize(face_normal_unnormalized);
        glm::vec3 face_center = (position_a + position_b + position_c) / 3.0f;
        glm::vec3 outward_direction = face_center - mesh_center;
        F32 outward_alignment = glm::dot(face_normal, outward_direction);
        CHECK(outward_alignment > 0.0f);

        for (U64 triangle_vertex_idx = 0; triangle_vertex_idx < 3; ++triangle_vertex_idx)
        {
            U16 vertex_idx = mesh.indices.data[index_idx + triangle_vertex_idx];
            glm::vec3 vertex_normal = mesh.vertices.data[vertex_idx].normal;
            CHECK(vertex_normal.x == doctest::Approx(face_normal.x));
            CHECK(vertex_normal.y == doctest::Approx(face_normal.y));
            CHECK(vertex_normal.z == doctest::Approx(face_normal.z));
        }
    }
}

TEST_CASE("cylinder mesh extends from the origin with outward-facing geometry")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));

    F32 radius = 0.5f;
    F32 height = 7.0f;
    constexpr U32 side_count = 8;
    glm::vec4 color = glm::vec4(0.8f, 0.3f, 0.1f, 1.0f);
    render::PrimitiveMesh mesh = geometry::cylinder_mesh_create(arena, radius, height, side_count, color);

    REQUIRE(mesh.vertices.size == side_count * 4 + 2);
    REQUIRE(mesh.indices.size == side_count * 12);
    U32 bottom_center_idx = side_count * 2;
    CHECK(mesh.vertices.data[bottom_center_idx].pos.x == doctest::Approx(0.0f));
    CHECK(mesh.vertices.data[bottom_center_idx].pos.y == doctest::Approx(0.0f));
    CHECK(mesh.vertices.data[bottom_center_idx].pos.z == doctest::Approx(0.0f));

    F32 min_height = max_f32;
    F32 max_height = -max_f32;
    for (U64 vertex_idx = 0; vertex_idx < mesh.vertices.size; ++vertex_idx)
    {
        render::PrimitiveVertex& vertex = mesh.vertices.data[vertex_idx];
        min_height = Min(min_height, vertex.pos.z);
        max_height = Max(max_height, vertex.pos.z);
        CHECK(glm::length(vertex.normal) == doctest::Approx(1.0f));
        CHECK(vertex.color.x == doctest::Approx(color.x));
        CHECK(vertex.color.y == doctest::Approx(color.y));
        CHECK(vertex.color.z == doctest::Approx(color.z));
        CHECK(vertex.color.w == doctest::Approx(color.w));
    }
    CHECK(min_height == doctest::Approx(0.0f));
    CHECK(max_height == doctest::Approx(height));

    glm::vec3 cylinder_center = glm::vec3(0.0f, 0.0f, height * 0.5f);
    for (U64 index_idx = 0; index_idx < mesh.indices.size; index_idx += 3)
    {
        U16 index_a = mesh.indices.data[index_idx];
        U16 index_b = mesh.indices.data[index_idx + 1];
        U16 index_c = mesh.indices.data[index_idx + 2];
        REQUIRE(index_a < mesh.vertices.size);
        REQUIRE(index_b < mesh.vertices.size);
        REQUIRE(index_c < mesh.vertices.size);

        glm::vec3 position_a = mesh.vertices.data[index_a].pos;
        glm::vec3 position_b = mesh.vertices.data[index_b].pos;
        glm::vec3 position_c = mesh.vertices.data[index_c].pos;
        glm::vec3 face_normal = glm::cross(position_b - position_a, position_c - position_a);
        glm::vec3 face_center = (position_a + position_b + position_c) / 3.0f;
        glm::vec3 outward_direction = face_center - cylinder_center;
        F32 outward_alignment = glm::dot(face_normal, outward_direction);
        CHECK(outward_alignment > 0.0f);
    }
}
