#include "../test_inc.hpp"
#include <thread>

TEST_CASE("mutex try take respects nested recording ownership")
{
    OS_Handle mutex = OS_MutexAlloc();
    defer(OS_MutexRelease(mutex));
    os_mutex_take(mutex);
    os_mutex_take(mutex);
    int lock_depth = 2;
    defer(for (; lock_depth > 0; --lock_depth) os_mutex_drop(mutex));

    // Cleanup on another thread must defer until every nested recording has ended.
    for (int remaining = 2; remaining >= 0; --remaining)
    {
        B32 acquired = false;
        std::thread worker([&] {
            acquired = os_mutex_try_take(mutex);
            if (acquired) os_mutex_drop(mutex);
        });
        worker.join();
        CHECK(acquired == (remaining == 0));
        if (remaining > 0)
        {
            os_mutex_drop(mutex);
            --lock_depth;
        }
    }
    CHECK(os_mutex_try_take(OS_HandleIsZero()));
}
