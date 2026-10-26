#include "../test_inc.hpp"
#include "async/thread_pool.cpp"
#include "async/async_http.cpp"
#include "test_http.hpp"

TEST_CASE("HTTP failures stop at their retry budget and preserve the error")
{
    auto task = std::unique_ptr<async::AsyncTaskStatus<U32>>(new async::AsyncTaskStatus<U32>(Allocator::create<async::AsyncTaskStatus<U32>>()));
    Arena* arena = task->allocator.arena;
    task->ext_type = async::ExtensionType::Http;
    auto* http = task->allocator.make<async::AsyncHttpTaskState<U32>>();
    task->http_ext = http;
    http->curl_ctx = async::_curl_ctx_create(arena);
    defer(async::_curl_context_cleanup(http->curl_ctx));
    http->task_start_us = os_now_microseconds();
    http->timeout_us = 60'000'000;
    http->max_http_retries = 2;
    http->max_task_retries = 1;
    U32 callback_calls = 0;
    task->user_data = &callback_calls;
    http->first_func = _test_http_callback_failure;
    http->next_func = http->first_func;

    String8 file = str8_path_from_str8_list(arena, {os_get_process_info()->binary_path, S("http-retry-test.txt")});
    bool callback_failure = false;
    SUBCASE("curl transfer failure") {}
    SUBCASE("user callback failure")
    {
        callback_failure = true;
        REQUIRE(os_write_data_to_file_path(file, S("payload")));
    }
    defer(if (callback_failure) os_delete_file_at_path(file));
#if OS_WINDOWS
    String8 url = push_str8f(arena, "file:///%.*s", str8_varg(file));
#else
    String8 url = push_str8f(arena, "file://%.*s", str8_varg(file));
#endif
    for (U64 index = 0; index < url.size; ++index)
    {
        if (url.str[index] == '\\') url.str[index] = '/';
    }
    http->first_http_info = async::http_info_create_get(arena, url);
    http->next_http_info = http->first_http_info;
    REQUIRE_FALSE(task->http_reconfigure(http->first_http_info));

    U32 iterations = 0;
    async::AsyncTaskContinuation<U32> next = {};
    do
    {
        next = async::_http_main<U32>({}, task.get());
        ++iterations;
    } while (next.func && iterations < 16);
    CHECK(next.func == nullptr);
    CHECK(iterations == 6); // Initial attempt plus two retries, then a full-task retry and two more.
    CHECK(task->error);
    CHECK(http->cur_task_retry_count == 1);
    CHECK(http->cur_http_retry_count == 2);
    if (callback_failure)
    {
        CHECK(callback_calls == 6);
        CHECK(http->error.result == async::AsyncResult::UserFunctionError);
        CHECK(http->error.error_str == "callback failed");
    }
    else
    {
        CHECK(callback_calls == 0);
        CHECK(http->error.result == async::AsyncResult::CurlError);
        CHECK(http->error.error_code == (U32)CURLE_FILE_COULDNT_READ_FILE);
    }
}

TEST_CASE("HTTP setup failures and rejected scheduling release allocator objects")
{
    U32 destroyed = 0;
    U32 callback_calls = 0;
    async::ThreadPool stopped_pool = {};
    Allocator allocator = Allocator::create();
    allocator.make<ArenaAwareTestObject>(U32{1}, &destroyed);
    auto* info = async::http_info_create_get(allocator.arena, S("http://127.0.0.1/"));
    async::AsyncHttpTaskStateConfig<U32> config(_test_http_callback_failure, &callback_calls, 0, 0);
    bool setup_failure = false;
    SUBCASE("invalid URL")
    {
        setup_failure = true;
        info->http_path = S("://invalid");
    }
    SUBCASE("queue rejected") {}
    auto result = async::async_http_task_run(std::move(allocator), &stopped_pool, info, &config, "not scheduled");
    if (setup_failure)
    {
        CHECK(result.async_result.has_error());
        CHECK(result.task_state == nullptr);
        CHECK(destroyed == 1);
    }
    else
    {
        CHECK_FALSE(result.async_result.has_error());
        CHECK(destroyed == 0);
        auto completed = async::async_task_take_completed(result.task_state);
        REQUIRE(completed);
        CHECK(completed->error);
        completed.reset();
        CHECK(destroyed == 1);
    }
    CHECK(callback_calls == 0);
}

static async::UserFuncResult<U32>
_test_http_callback_failure(Arena* arena, async::ThreadPool*, String8, U32* calls)
{
    ++*calls;
    return async::UserFuncResult<U32>::failure(arena, "callback failed");
}
