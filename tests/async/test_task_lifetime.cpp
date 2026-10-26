#include "../test_inc.hpp"
#include "async/async_task_templates.hpp"

TEST_CASE("async task result owns allocator objects until consumption")
{
    U32 destruction_count = 0;
    auto* task = async::_async_task_status_create<int>(Allocator::create(), nullptr, S("lifetime"), nullptr);
    task->allocator.make<ArenaAwareTestObject>(U32{7}, &destruction_count);
    task->http_ext = task->allocator.make<async::AsyncHttpTaskState<int>>();
    task->ext_type = async::ExtensionType::Http;
    task->http_ext->error = async::AsyncGenericError(async::AsyncResult::UserFunctionError);
    task->http_ext->error.error_str.assign(256, 'x');
    task->add_error();

    {
        auto pending = async::async_task_take_completed(task);
        CHECK_FALSE(pending);
    }
    CHECK(destruction_count == 0);
    task->done.store(true, std::memory_order_release);
    {
        auto result = async::async_task_take_completed(task);
        REQUIRE(result);
        CHECK_FALSE(async::async_task_take_completed(task));
        CHECK(result->error);
        CHECK(result->http_ext->error.error_str.size() == 256);
        auto moved = std::move(result);
        CHECK(result == nullptr);
        CHECK(moved.get() == task);
        CHECK(destruction_count == 0);
    }
    CHECK(destruction_count == 1);
}

TEST_CASE("async task result move assignment releases each allocator once")
{
    U32 first_destroyed = 0;
    U32 second_destroyed = 0;
    auto* first = new async::AsyncTaskStatus<int>(Allocator::create<async::AsyncTaskStatus<int>>());
    auto* second = new async::AsyncTaskStatus<int>(Allocator::create<async::AsyncTaskStatus<int>>());
    first->allocator.make<ArenaAwareTestObject>(U32{1}, &first_destroyed);
    second->allocator.make<ArenaAwareTestObject>(U32{2}, &second_destroyed);
    first->done.store(true, std::memory_order_release);
    second->done.store(true, std::memory_order_release);
    {
        auto first_result = async::async_task_take_completed(first);
        auto second_result = async::async_task_take_completed(second);
        REQUIRE(first_result);
        REQUIRE(second_result);
        CHECK_FALSE(first_result->error);
        first_result = std::move(second_result);
        CHECK(first_destroyed == 1);
        CHECK(second_destroyed == 0);
        CHECK(first_result.get() == second);
        CHECK(second_result == nullptr);
    }
    CHECK(first_destroyed == 1);
    CHECK(second_destroyed == 1);
}

