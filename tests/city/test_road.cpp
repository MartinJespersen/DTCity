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

template <typename T>
MappedHandle<T>
mapped_buffer_create(Arena* arena, ThreadWorkerCmdCtx*, BufferType, String8, U64 element_count)
{
    MappedHandle<T> result = {};
    result.buffer = buffer_alloc<MappedHandleFrame<T>>(arena, MAX_FRAMES_IN_FLIGHT);
    for (auto& frame : result.buffer)
    {
        frame.data = buffer_alloc<T>(arena, element_count).data;
        frame.handle = Handle((void*)(++road_renderer.created), 1, HandleType::Buffer);
    }
    return result;
}

template <typename T>
void
mapped_buffer_add(MappedHandle<T> handle, Buffer<T> data)
{
    ++road_renderer.count_updates;
    MemoryCopy(handle.buffer[road_renderer.frame_index].data, data.data, data.size * sizeof(T));
}
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
        REQUIRE(road->way_ids.size == 2);
        CHECK(road->way_ids[1] == 123);
        CHECK(road->pending_bvh.road_segment_buffer_sorted[0].way_index == 1);
        CHECK(road_renderer.created == 2 + render::MAX_FRAMES_IN_FLIGHT);
    }
    CHECK(road_renderer.destroyed == 2 + render::MAX_FRAMES_IN_FLIGHT);
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
        task.result->way_ids = buffer_alloc<osm::WayId>(task.result->allocator.arena, 2);
        task.result->way_ids[1] = 123;
        task.result->pending_bvh.road_segment_buffer_sorted[0].way_index = 1;
        city::_road_upload(task.result.get());
        CHECK(road_renderer.created == 2 + render::MAX_FRAMES_IN_FLIGHT);
        CHECK(road_renderer.submitted == 1);
        handle = context.publish(&task, &bvhs);
        CHECK(task.result->allocator.arena == nullptr);
        CHECK(task.result->pending_bvh.arena == nullptr);
    }
    CHECK(road_renderer.destroyed == 0);
    CHECK(road_renderer.created == 2 + render::MAX_FRAMES_IN_FLIGHT);
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    render::TilePipelineData pipeline = {};
    context.pipeline_add(handle, &pipeline, city::RoadOverlayOption_Bikeability_ft);
    CHECK_FALSE(pipeline.road_test_enabled);
    road_renderer.loaded[1] = true;
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    road_renderer.loaded[2] = true;
    CHECK_FALSE(context.update(handle, city::RoadOverlayOption_None));
    for (U32 index = 0; index < render::MAX_FRAMES_IN_FLIGHT; ++index)
        road_renderer.loaded[3 + index] = true;
    CHECK(context.update(handle, city::RoadOverlayOption_None));
    context.pipeline_add(handle, &pipeline, city::RoadOverlayOption_Bikeability_ft);
    CHECK(pipeline.road_test_enabled);
    city::Road* road = {};
    REQUIRE(context.get(handle, &road));
    CHECK(pipeline.colormap_handle.u64 == road->colormap_handle.u64);
    CHECK(pipeline.road_segment_buffer_handle.u64 == road->segment_buffer_handle.u64);
    CHECK(pipeline.way_agent_count_buffer.buffer.data == road->way_agent_count_buffer.buffer.data);
    PoolHandle bvh_handle = road->bvh_handle;
    city::RoadBvh* bvh = {};
    REQUIRE(bvhs.item_from_handle(bvh_handle, &bvh));
    bvh->loads_in_flight = 1;
    context.release(handle, &bvhs);
    CHECK(road_renderer.destroyed == 2 + render::MAX_FRAMES_IN_FLIGHT);
    CHECK_FALSE(context.get(handle, &road));
    REQUIRE(bvhs.item_from_handle(bvh_handle, &bvh));
    CHECK(bvh->deletion_requested);
    CHECK(bvh->arena != nullptr);
    // Simulate the tile loader retiring its final borrowed BVH reference.
    --bvh->loads_in_flight;
    arena_release(bvh->arena);
    bvhs.item_free(bvh_handle);
    context.release(handle, &bvhs);
    CHECK(road_renderer.destroyed == 2 + render::MAX_FRAMES_IN_FLIGHT);
}

TEST_CASE("Way counts share indices across sorted segments and clear every frame buffer")
{
    road_renderer = {};
    Allocator owner = Allocator::create();
    city::RoadContext context;
    context.init(owner.arena, 1);
    ResourcePool<city::RoadBvh> bvhs(owner.arena, 1);
    city::RoadBuildTask task = {};
    task.result = std::make_unique<city::Road>();
    osm::Edge edges[] = {
        {.id = 1, .way_id = 123}, {.id = 2, .way_id = 0}, {.id = 3, .way_id = 123},
        {.id = 4, .way_id = -7}, {.id = 5, .way_id = 0},
    };
    Buffer<city::RoadSegmentCorners> segments = buffer_alloc<city::RoadSegmentCorners>(owner.arena, ArrayCount(edges));
    for (U32 index = 0; index < segments.size; ++index)
    {
        segments[index].edge_id = edges[index].id;
        for (U32 corner = 0; corner < 4; ++corner)
            segments[index].corners[corner] = vec_2f32((5 - index) * 10 + (corner & 1), (corner >> 1) & 1);
    }
    task.result->way_ids = city::_road_way_indices_build(task.result->allocator.arena,
        {.data = edges, .size = ArrayCount(edges)}, segments);
    REQUIRE(task.result->way_ids.size == 4);
    CHECK(segments[0].way_index == segments[2].way_index);
    CHECK(segments[1].way_index == segments[4].way_index);
    CHECK(segments[1].way_index != 0);
    city::_road_bvh_create(&task.result->pending_bvh, segments, 1);
    for (auto segment : task.result->pending_bvh.road_segment_buffer_sorted)
    {
        REQUIRE(segment.way_index > 0);
        REQUIRE(segment.way_index < task.result->way_ids.size);
        CHECK(task.result->way_ids[segment.way_index] == edges[segment.edge_id - 1].way_id);
    }
    city::_road_upload(task.result.get());
    PoolHandle handle = context.publish(&task, &bvhs);
    for (U64 index = 1; index <= road_renderer.created; ++index) road_renderer.loaded[index] = true;
    REQUIRE(context.update(handle, city::RoadOverlayOption_None));
    city::Road* road = {};
    REQUIRE(context.get(handle, &road));
    Map<osm::WayId, U32> counts(&owner, 8);
    counts.insert(owner.arena, 123, 2);
    counts.insert(owner.arena, 0, 5);
    counts.insert(owner.arena, 999, 42); // A simulation way outside the rendered network.
    for (U32 frame = 0; frame < render::MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        road_renderer.frame_index = frame;
        context.agent_counts_update(handle, &counts);
        auto* data = road->way_agent_count_buffer.buffer[frame].data;
        CHECK(data[0].agent_count == 0);
        CHECK(data[1].agent_count == 2);
        CHECK(data[2].agent_count == 5);
        CHECK(data[3].agent_count == 0);
    }
    counts.clear();
    for (U32 frame = 0; frame < render::MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        road_renderer.frame_index = frame;
        context.agent_counts_update(handle, &counts);
        auto* data = road->way_agent_count_buffer.buffer[frame].data;
        for (U64 index = 0; index < road->way_ids.size; ++index) CHECK(data[index].agent_count == 0);
    }
    counts.insert(owner.arena, 123, 10);
    context.agent_counts_update(handle, &counts);
    context.agent_counts_update(handle, nullptr);
    CHECK(road->way_agent_count_buffer.buffer[road_renderer.frame_index].data[1].agent_count == 0);
    U64 count_updates = road_renderer.count_updates;
    context.release(handle, &bvhs);
    context.agent_counts_update(handle, &counts);
    CHECK(road_renderer.count_updates == count_updates);
    CHECK(road_renderer.destroyed == road_renderer.created);
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

