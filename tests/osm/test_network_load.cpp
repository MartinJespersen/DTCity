#define DOCTEST_CONFIG_IMPLEMENT
#include "core_inc.hpp"
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "third_party/doctest/doctest.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#include "base/base_inc.cpp"
#include "async/thread_pool.cpp"
#include "async/async_error.cpp"
#include "async/async_http.cpp"
#include "lib_wrappers/json.cpp"
#include "osm/osm.cpp"
#include "../base/test_pool_lifetime.cpp"
#include "../city/test_neta_load.cpp"
#include "../city/test_road.cpp"

TEST_CASE("Network parse result survives transfer into a pool and task destruction")
{
    Allocator owner = Allocator::create();
    auto* pool = ResourcePool<osm::Network>::create(owner.arena, 1);
    osm::Network* network = {};
    PoolHandle handle = {};
    {
        Allocator task_allocator = Allocator::create();
        auto* input = task_allocator.make<osm::NetworkLoadTask>();
        input->node_hashmap_size = 8;
        input->way_hashmap_size = 8;
        String8 json = push_str8_copy(task_allocator.arena, S(R"({"elements":[
            {"type":"node","id":1,"lat":59,"lon":16},
            {"type":"node","id":2,"lat":59.1,"lon":16.1},
            {"type":"way","id":123,"nodes":[1,2],"tags":{"highway":"residential"}}
        ]})"));
        REQUIRE_FALSE(osm::_network_parse(input, json));
        REQUIRE(input->result);
        REQUIRE(input->result->edge_structure.edges.size == 1);
        handle = pool->emplace(std::move(*input->result));
        REQUIRE(pool->item_from_handle(handle, &network));
        CHECK(input->result->allocator.arena == nullptr);
        MemoryZero(json.str, json.size);
    }
    defer(pool->item_free(handle));
    CHECK(network->edge_structure.edges[0].way_id == 123);
    CHECK(network->edge_structure.edges[0].node_id_from == 1);
    CHECK(network->edge_structure.edges[0].node_id_to == 2);
    CHECK(osm::wgs_location_get(network, 1).lat == 59);
    CHECK(str8_match(osm::way_find(network, 123)->way.tags[0].value, S("residential"), 0));
    CHECK(pool->is_handle_valid(handle));
}

TEST_CASE("Network parsing can exceed the default four MiB arena reservation")
{
    constexpr U32 node_count = 30000;
    std::string json = "{\"elements\":[";
    for (U32 index = 1; index <= node_count; ++index)
    {
        json += "{\"type\":\"node\",\"id\":" + std::to_string(index) +
                ",\"lat\":59,\"lon\":16},";
    }
    json += R"({"type":"way","id":123,"tags":{"highway":"residential"},"nodes":[)";
    for (U32 index = 1; index <= node_count; ++index)
    {
        if (index > 1) json += ',';
        json += std::to_string(index);
    }
    json += "]}]}";
    osm::NetworkLoadTask input = {};
    input.node_hashmap_size = 1000;
    input.way_hashmap_size = 100;
    REQUIRE_FALSE(osm::_network_parse(&input, str8((U8*)json.data(), json.size())));
    REQUIRE(input.result);
    CHECK(arena_pos(input.result->allocator.arena) > MB(4));
    CHECK(input.result->allocator.arena->cmt < input.result->allocator.arena->res);
    CHECK(input.result->edge_structure.edges.size == node_count - 1);
    CHECK(input.result->node_id_arr[enum_idx(osm::WayType::Highway)].size == node_count);
    CHECK(input.result->edge_structure.edges[node_count - 2].node_id_to == node_count);
}

TEST_CASE("Network load parses cached input and rejects malformed input without publishing")
{
    Allocator owner = Allocator::create();
    String8 root = str8_path_from_str8_list(owner.arena,
        {os_get_process_info()->binary_path, S("test-network-load")});
    REQUIRE(os_make_directory(root));
    defer(os_delete_directory_at_path(root));
    String8 file = str8_path_from_str8_list(owner.arena, {root, S("osm.json")});
    String8 meta = str8_concat(owner.arena, file, S(".meta"));
    defer(os_delete_file_at_path(file));
    defer(os_delete_file_at_path(meta));
    bool malformed = false;
    SUBCASE("Valid empty network") {}
    SUBCASE("Malformed cache") { malformed = true; }
    REQUIRE_FALSE(cache_write(file, malformed ? S("invalid json") : S(R"({"elements":[]})"), S("bbox")));
    // Execute the queued worker deterministically after the caller's strings expire.
    auto* pool = async::thread_pool_create(owner.arena, 0, 8, 8);
    defer(async::thread_pool_destroy(pool));
    // Advertise the manually driven worker so task submission accepts this pool.
    pool->thread_count = 1;
    async::AsyncTaskStatus<osm::NetworkLoadTask>* pending = {};
    {
        Allocator request = Allocator::create();
        osm::NetworkCache cache = {
            .file_location = push_str8_copy(request.arena, file),
            .bbox_str = push_str8_copy(request.arena, S("bbox")),
        };
        pending = osm::network_load(pool, cache, S("request"), 8, 8);
        CHECK_FALSE(pending->done.load());
        CHECK_FALSE(pending->user_data->result);
    }
    async::WorkerItem work = {};
    REQUIRE(async::_thread_pool_try_get_work(pool, &work));
    pool->pending_task_count.fetch_sub(1);
    auto next = work.func({.thread_pool = pool}, work.user_data);
    CHECK(next.next_task.func == nullptr);
    auto task = async::async_task_take_completed(pending);
    REQUIRE(task);
    CHECK((bool)task->error == malformed);
    CHECK((bool)task->user_data->result == !malformed);
    CHECK(task->ext_type == async::ExtensionType::None);
}

TEST_CASE("Cache misses configure HTTP and downloaded results have one owner")
{
    Allocator owner = Allocator::create();
    String8 root = str8_path_from_str8_list(owner.arena,
        {os_get_process_info()->binary_path, S("test-network-download")});
    REQUIRE(os_make_directory(root));
    defer(os_delete_directory_at_path(root));
    String8 file = str8_path_from_str8_list(owner.arena, {root, S("osm.json")});
    String8 meta = str8_concat(owner.arena, file, S(".meta"));
    defer(os_delete_file_at_path(file));
    defer(os_delete_file_at_path(meta));
    bool malformed = false;
    SUBCASE("Valid download") {}
    SUBCASE("Malformed download") { malformed = true; }
    auto task = std::unique_ptr<async::AsyncTaskStatus<osm::NetworkLoadTask>>(
        async::_async_task_status_create<osm::NetworkLoadTask>(Allocator::create(), nullptr, S("download test"), nullptr));
    auto* input = task->allocator.make<osm::NetworkLoadTask>();
    input->cache = {.file_location = file, .bbox_str = S("bbox")};
    input->request_body = S("request");
    input->node_hashmap_size = 8;
    input->way_hashmap_size = 8;
    task->user_data = input;
    auto next = osm::_network_load_begin({}, task.get());
    REQUIRE(task->ext_type == async::ExtensionType::Http);
    defer(async::_curl_context_cleanup(task->http_ext->curl_ctx));
    REQUIRE_FALSE(task->error);
    CHECK(next.func != nullptr);
    CHECK_FALSE(input->result);
    CHECK(task->http_ext->max_http_retries == 3);
    // Exercise the response callback without contacting Overpass.
    auto response = task->http_ext->next_func(task->allocator.arena, nullptr,
        malformed ? S("invalid json") : S(R"({"elements":[]})"), input);
    CHECK((bool)response.successful == !malformed);
    CHECK((bool)input->result == !malformed);
    CHECK_FALSE(cache_read(owner.arena, file, S("bbox")).err);
}

int
App(int argc, char** argv)
{
    return doctest::Context(argc, argv).run();
}
