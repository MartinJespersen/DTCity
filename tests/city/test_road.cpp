#include "test_road.hpp"
#include "render/render_templates.hpp"
#include "misc/geometry.cpp"
#include "city/road.cpp"

static RoadTestRenderer road_renderer = {};

// Deterministic upload backend: readiness is controlled independently of CPU completion.
namespace render
{
ThreadWorkerCmdCtx* thread_ctx_create() { return &road_renderer.commands; }
void thread_cmd_buffer_record(ThreadWorkerCmdCtx*) {}
void thread_cmd_buffer_end(ThreadWorkerCmdCtx*) { ++road_renderer.submitted; }
Handle _buffer_load_sync(ThreadWorkerCmdCtx*, BufferInfo*, String8)
{
    return Handle((void*)(++road_renderer.created), 1, HandleType::Buffer);
}
bool is_handle_zero(Handle handle) { return handle.u64 == 0; }
bool is_resource_loaded(Handle handle) { return road_renderer.loaded[handle.u64]; }
void handle_destroy_deferred(Handle handle)
{
    if (!handle.u64) return;
    CHECK_FALSE(road_renderer.released[handle.u64]);
    road_renderer.released[handle.u64] = true;
    ++road_renderer.destroyed;
}
void tile_pipeline_add(TilePipelineData*) {}
}

TEST_CASE("Road worker owns its input path and builds a private persistent result")
{
    road_renderer = {};
    Allocator owner = Allocator::create();
    String8 file = str8_path_from_str8_list(owner.arena,
        {os_get_process_info()->binary_path, S("test-road-netascore.json")});
    String8 meta = str8_concat(owner.arena, file, S(".meta"));
    defer(os_delete_file_at_path(file));
    defer(os_delete_file_at_path(meta));
    REQUIRE_FALSE(cache_write(file, S(R"({"type":"FeatureCollection","features":[]})"), S("roads")));
    osm::Network network(8, 8);
    network.edge_structure.edges = buffer_alloc<osm::Edge>(network.allocator.arena, 1);
    network.edge_structure.edges[0] = {.id = 9, .node_id_from = 1, .node_id_to = 2, .way_id = 123};
    network.ecef_location_map->insert(network.allocator.arena, 1, osm::ecef_location_create(1, vec_3f64(0, 0, 0)));
    network.ecef_location_map->insert(network.allocator.arena, 2, osm::ecef_location_create(2, vec_3f64(10, 10, 0)));
    auto* workers = async::thread_pool_create(owner.arena, 0, 8, 8);
    defer(async::thread_pool_destroy(workers));
    workers->thread_count = 1;
    city::RoadContext context;
    context.init(owner.arena, 1);
    async::AsyncTaskStatus<city::RoadBuildTask>* pending = {};
    {
        Allocator request = Allocator::create();
        city::RoadBuildInput input = {.network = &network,
            .netascore_file_path = push_str8_copy(request.arena, file),
            .ecef_to_local = glm::dmat4(1.0), .default_road_width = 2};
        pending = context.create(workers, input);
        CHECK_FALSE(pending->user_data->result);
    }
    async::WorkerItem work = {};
    REQUIRE(async::_thread_pool_try_get_work(workers, &work));
    workers->pending_task_count.fetch_sub(1);
    work.func({.thread_pool = workers}, work.user_data);
    {
        auto completed = async::async_task_take_completed(pending);
        REQUIRE(completed);
        REQUIRE_FALSE(completed->error);
        REQUIRE(completed->user_data->result);
        city::Road* road = completed->user_data->result.get();
        REQUIRE(road->pending_bvh.road_segment_buffer_sorted.size == 1);
        CHECK(road->pending_bvh.road_segment_buffer_sorted[0].edge_id == 9);
        CHECK(road->pending_bvh.root != nullptr);
        REQUIRE(road->road_info_map != nullptr);
        CHECK(road->road_info_map->get(9) == nullptr);
        CHECK(road_renderer.created == 2);
    }
    CHECK(road_renderer.destroyed == 2);
}

TEST_CASE("Road publication transfers ownership and waits for upload readiness")
{
    road_renderer = {};
    Allocator owner = Allocator::create();
    city::RoadContext context;
    context.init(owner.arena, 1);
    ResourcePool<city::RoadBvh> bvhs(owner.arena, 1);
    PoolHandle handle = {};
    {
        city::RoadBuildTask task = {};
        task.result = std::make_unique<city::Road>();
        task.result->pending_bvh.road_segment_buffer_sorted =
            buffer_alloc<city::RoadSegmentCorners>(task.result->pending_bvh.arena, 1);
        city::_road_upload(task.result.get());
        CHECK(road_renderer.created == 2);
        CHECK(road_renderer.submitted == 1);
        handle = context.publish(&task, &bvhs);
        CHECK(task.result->allocator.arena == nullptr);
        CHECK(task.result->pending_bvh.arena == nullptr);
    }
    CHECK(road_renderer.destroyed == 0);
    CHECK(road_renderer.created == 2);
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    render::TilePipelineData pipeline = {};
    context.pipeline_add(handle, &pipeline, city::RoadOverlayOption_Bikeability_ft);
    CHECK_FALSE(pipeline.road_test_enabled);
    road_renderer.loaded[1] = true;
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    road_renderer.loaded[2] = true;
    CHECK(context.update(handle, city::RoadOverlayOption_None));
    context.pipeline_add(handle, &pipeline, city::RoadOverlayOption_Bikeability_ft);
    CHECK(pipeline.road_test_enabled);
    city::Road* road = {};
    REQUIRE(context.get(handle, &road));
    CHECK(pipeline.colormap_handle.u64 == road->colormap_handle.u64);
    CHECK(pipeline.road_segment_buffer_handle.u64 == road->segment_buffer_handle.u64);
    PoolHandle bvh_handle = road->bvh_handle;
    city::RoadBvh* bvh = {};
    REQUIRE(bvhs.item_from_handle(bvh_handle, &bvh));
    bvh->loads_in_flight = 1;
    context.release(handle, &bvhs);
    CHECK(road_renderer.destroyed == 2);
    CHECK_FALSE(context.get(handle, &road));
    REQUIRE(bvhs.item_from_handle(bvh_handle, &bvh));
    CHECK(bvh->deletion_requested);
    CHECK(bvh->arena != nullptr);
    // Simulate the tile loader retiring its final borrowed BVH reference.
    --bvh->loads_in_flight;
    arena_release(bvh->arena);
    bvhs.item_free(bvh_handle);
    context.release(handle, &bvhs);
    CHECK(road_renderer.destroyed == 2);
}

TEST_CASE("Discarded road results clean up uploads and empty roads remain unavailable")
{
    road_renderer = {};
    Allocator owner = Allocator::create();
    city::RoadContext context;
    context.init(owner.arena, 1);
    ResourcePool<city::RoadBvh> bvhs(owner.arena, 1);
    {
        city::RoadBuildTask task = {};
        task.result = std::make_unique<city::Road>();
        task.result->segment_buffer_handle = render::_buffer_load_sync({}, {}, {});
    }
    CHECK(road_renderer.destroyed == 1);
    city::RoadBuildTask task = {};
    task.result = std::make_unique<city::Road>();
    city::_road_bvh_create(&task.result->pending_bvh, {}, 10);
    city::_road_upload(task.result.get());
    PoolHandle handle = context.publish(&task, &bvhs);
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    CHECK(road_renderer.created == 1);
    city::Road* road = {};
    REQUIRE(context.get(handle, &road));
    PoolHandle bvh_handle = road->bvh_handle;
    context.release(handle, &bvhs);
    CHECK_FALSE(bvhs.is_handle_valid(bvh_handle));
}

