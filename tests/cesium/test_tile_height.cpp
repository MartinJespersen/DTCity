#include "../test_inc.hpp"
#include "../render/tile_bvh_test_backend.hpp"
#include "base/base_allocator.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "base/container.hpp"
#include "city/tile_bvh.hpp"
#include "city/agent.hpp"
#include "render/tile_bvh.hpp"
#include "shaders/shader_shared.h"
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

TEST_CASE("tile height sampler preserves IDs through partial and out of order completion")
{
    Allocator allocator = Allocator::create();
    city::TileHeightSampler sampler(&allocator, 3);
    defer(sampler.destroy());
    U32 first = tile_bvh_test::height_batch_count - 1;
    U32 second = first + 1;
    render::TileBvhHandle resource = render::tile_bvh_resource_create({}, {}, {});
    defer(render::tile_bvh_resource_destroy(resource));
    city::TileHeightQuery queries[3] = {};
    for (U32 index = 0; index < 3; ++index)
    {
        queries[index].request_id = 100 + index;
        queries[index].ray_origin = {1, 2, 50};
        queries[index].ray_direction = {0, 0, -1};
    }
    CHECK(sampler.submit(resource, {queries, 3}));
    CHECK(tile_bvh_test::height_batches[first].queries.data[0].origin[2] == 50);
    CHECK(tile_bvh_test::height_batches[first].queries.data[0].direction[2] == -1);
    queries[0].request_id = 900;
    CHECK(sampler.submit(resource, {queries, 1}));
    CHECK_FALSE(sampler.submit(resource, {queries, 1}));
    city::TileHeightResult result[2] = {};
    CHECK(sampler.collect({result, 2}) == 0);
    tile_bvh_test::height_batches[second].results.data[0] = {7, 1, 42, 8};
    tile_bvh_test::tile_height_test_complete(second);
    REQUIRE(sampler.collect({result, 2}) == 1);
    CHECK(result[0].request_id == 900);
    CHECK(result[0].hit_distance == 7);
    CHECK(result[0].entry_handle.idx == 42);
    CHECK(result[0].entry_handle.gen_id == 8);
    auto& batch = tile_bvh_test::height_batches[first];
    batch.results.data[0] = {1, 1, 11, 3};
    batch.results.data[1] = {0, 0, 0, 0};
    batch.results.data[2] = {3, 1, 11, 4}; // Same registry slot, different generation.
    tile_bvh_test::tile_height_test_complete(first);
    REQUIRE(sampler.collect({result, 2}) == 2);
    CHECK(result[0].request_id == 100);
    CHECK(result[0].entry_handle.idx == 11);
    CHECK(result[0].entry_handle.gen_id == 3);
    CHECK(result[1].request_id == 101);
    CHECK(result[1].hit == 0);
    CHECK(result[1].entry_handle.idx == 0);
    CHECK(result[1].entry_handle.gen_id == 0);
    REQUIRE(sampler.collect({result, 2}) == 1);
    CHECK(result[0].request_id == 102);
    CHECK(result[0].hit_distance == 3);
    CHECK(result[0].entry_handle.idx == 11);
    CHECK(result[0].entry_handle.gen_id == 4);
    CHECK(sampler.collect({result, 2}) == 0);
}

TEST_CASE("tile height sampler retirement keeps an outstanding BVH alive")
{
    Allocator allocator = Allocator::create();
    city::TileHeightSampler sampler(&allocator, 1);
    U32 batch = tile_bvh_test::height_batch_count - 1;
    render::TileBvhHandle resource = render::tile_bvh_resource_create({}, {}, {});
    city::TileHeightQuery query = {7, {0, 0, 10}, {0, 0, -1}};
    REQUIRE(sampler.submit(resource, {&query, 1}));
    render::tile_bvh_resource_destroy(resource);
    sampler.destroy();
    CHECK(tile_bvh_test::backend.resources[resource.idx].references == 1);
    tile_bvh_test::tile_height_test_complete(batch);
    CHECK(tile_bvh_test::backend.resources[resource.idx].references == 0);
}

TEST_CASE("tile height ray starts above bounds and preserves ellipsoid height units")
{
    Rng3F32 bounds = {};
    bounds.min = {-50, -30, -100};
    bounds.max = {50, 30, 200};
    glm::dvec3 surface(4, 5, -7);
    glm::dvec3 up = glm::normalize(glm::dvec3(1, 2, 3));
    city::TileHeightRay ray = city::tile_height_ray_create(surface, up, bounds);
    CHECK(glm::length(ray.query.ray_direction) == doctest::Approx(1));
    for (U32 corner = 0; corner < 8; ++corner)
    {
        glm::dvec3 point;
        for (U32 axis = 0; axis < 3; ++axis)
            point[axis] = (corner & (1u << axis)) ? bounds.max.v[axis] : bounds.min.v[axis];
        CHECK(glm::dot(point - surface, up) < ray.origin_height);
    }
    // A hit 12.5 metres above the ellipsoid must recover that geodetic height.
    F64 distance = ray.origin_height - 12.5;
    CHECK(ray.origin_height - distance == doctest::Approx(12.5));
    glm::dvec3 hit = glm::dvec3(ray.query.ray_origin) + glm::dvec3(ray.query.ray_direction) * distance;
    CHECK(glm::length(hit - (surface + up * 12.5)) < 0.001);
}

TEST_CASE("tile height results reject stale epochs agents requests and moved positions")
{
    city::Agent agent = {};
    agent.handle = {1, 3};
    agent.height = 7;
    agent.height_request_id = 100;
    agent.ecef_coord = {4000000, 3000000, 2000000};
    city::AgentHeightRequest request = {100, 9, agent.handle, agent.ecef_coord, 200};
    city::TileHeightResult result = {100, 180, true, {12, 3}};
    SUBCASE("complete current result")
    {
        CHECK(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.height == 20);
        CHECK(agent.height_request_id == 0);
        CHECK(agent.transform_dirty);
    }
    SUBCASE("unchanged height does not rebuild the transform")
    {
        agent.height = 20;
        CHECK(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK_FALSE(agent.transform_dirty);
        CHECK(agent.height_request_id == 0);
    }
    SUBCASE("unchanged height preserves a pending movement update")
    {
        agent.height = 20;
        agent.transform_dirty = true;
        CHECK(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.transform_dirty);
    }
    SUBCASE("streaming epoch changed")
    {
        CHECK_FALSE(city::agent_height_result_apply(&agent, request, result, 10));
        CHECK(agent.height == 7);
        CHECK(agent.height_request_id == 100);
    }
    SUBCASE("agent slot was reused")
    {
        agent.handle.gen_id = 4;
        CHECK_FALSE(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.height == 7);
    }
    SUBCASE("newer query already exists")
    {
        agent.height_request_id = 101;
        CHECK_FALSE(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.height_request_id == 101);
    }
    SUBCASE("agent moved too far")
    {
        agent.ecef_coord.x += 6;
        CHECK_FALSE(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.height == 7);
        CHECK(agent.height_request_id == 0);
    }
    SUBCASE("a miss keeps the old height")
    {
        result.hit = false;
        CHECK_FALSE(city::agent_height_result_apply(&agent, request, result, 9));
        CHECK(agent.height == 7);
        CHECK(agent.height_request_id == 0);
        CHECK_FALSE(agent.transform_dirty);
    }
}
