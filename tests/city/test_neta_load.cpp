#include "utility/geospatial.cpp"
#include "city/neta.cpp"

TEST_CASE("Neta cache input is task owned and its result survives task destruction")
{
    Allocator owner = Allocator::create();
    String8 root = str8_path_from_str8_list(owner.arena,
        {os_get_process_info()->binary_path, S("test-neta-load")});
    REQUIRE(os_make_directory(root));
    defer(os_delete_directory_at_path(root));
    String8 file = str8_path_from_str8_list(owner.arena, {root, S("edges.geojson")});
    String8 meta = str8_concat(owner.arena, file, S(".meta"));
    defer(os_delete_file_at_path(file));
    defer(os_delete_file_at_path(meta));
    REQUIRE_FALSE(cache_write(file, S(R"({"type":"FeatureCollection","features":[]})"), S("bbox")));
    auto* workers = async::thread_pool_create(owner.arena, 0, 8, 8);
    defer(async::thread_pool_destroy(workers));
    workers->thread_count = 1; // The test drives the queued worker without waits.
    async::AsyncTaskStatus<neta::NetaLoadTask>* pending = {};
    {
        Allocator request = Allocator::create();
        neta::NetaCache cache = {.file_location = push_str8_copy(request.arena, file),
                                 .bbox_str = push_str8_copy(request.arena, S("bbox"))};
        pending = neta::neta_load(workers, cache, {});
        CHECK_FALSE(pending->user_data->result);
    }
    ResourcePool<neta::NetaState> results(owner.arena, 1);
    PoolHandle handle = {};
    {
        async::WorkerItem work = {};
        REQUIRE(async::_thread_pool_try_get_work(workers, &work));
        workers->pending_task_count.fetch_sub(1);
        auto next = work.func({.thread_pool = workers}, work.user_data);
        CHECK(next.next_task.func == nullptr);
        auto completed = async::async_task_take_completed(pending);
        REQUIRE(completed);
        REQUIRE_FALSE(completed->error);
        CHECK(completed->ext_type == async::ExtensionType::None);
        CHECK(completed->user_data->mobility_api_key_header.size == 0);
        REQUIRE(completed->user_data->result);
        handle = results.emplace(std::move(*completed->user_data->result));
    }
    neta::NetaState* state = {};
    REQUIRE(results.item_from_handle(handle, &state));
    CHECK(str8_match(state->cache_file_location, file, 0));
    results.item_free(handle);
    CHECK_FALSE(results.is_handle_valid(handle));
}

TEST_CASE("Neta download publishes only after caching succeeds and supports retry")
{
    Allocator owner = Allocator::create();
    String8 root = str8_path_from_str8_list(owner.arena,
        {os_get_process_info()->binary_path, S("test-neta-download")});
    REQUIRE(os_make_directory(root));
    defer(os_delete_directory_at_path(root));
    String8 file = str8_path_from_str8_list(owner.arena, {root, S("edges.geojson")});
    String8 meta = str8_concat(owner.arena, file, S(".meta"));
    defer(os_delete_file_at_path(file));
    defer(os_delete_file_at_path(meta));
    neta::NetaLoadTask task = {};
    task.cache = {.file_location = file, .bbox_str = S("bbox")};
    neta::_netascore_download_enqueue(owner.arena, &task, S("edges"), S("city_edges.geojson"));
    String8 body = S(R"({"type":"FeatureCollection","features":[]})");
    {
        REQUIRE(os_make_directory(file));
        defer(os_delete_directory_at_path(file));
        auto failed = neta::_netascore_download_file_complete(owner.arena, nullptr, body, &task);
        CHECK_FALSE(failed.successful);
        CHECK_FALSE(task.result);
        CHECK(task.download_queue.first != nullptr);
    }
    auto downloaded = neta::_netascore_download_file_complete(owner.arena, nullptr, body, &task);
    REQUIRE(downloaded.successful);
    REQUIRE(task.result);
    CHECK(task.download_queue.first == nullptr);
    CHECK(str8_match(task.result->cache_file_location, file, 0));
    CHECK_FALSE(cache_read(owner.arena, file, S("bbox")).err);
}

TEST_CASE("Malformed Neta service responses do not publish a result")
{
    Allocator owner = Allocator::create();
    neta::NetaLoadTask task = {};
    CHECK_FALSE(neta::netascore_job_create_complete(owner.arena, nullptr, S("invalid"), &task).successful);
    CHECK_FALSE(neta::_netascore_job_status_complete(owner.arena, nullptr, S("invalid"), &task).successful);
    CHECK_FALSE(neta::_netascore_downloads_complete(owner.arena, nullptr, S("invalid"), &task).successful);
    CHECK_FALSE(task.result);
}
