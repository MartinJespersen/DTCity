#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest/doctest.h"
#include "test_arena_failure.hpp"

// Exercise production arena code with deterministic OS failures. No test needs
// to exhaust system memory or install hooks in the application.
#include "base/base_arena.cpp"
#include "base/base_allocator.cpp"

g_internal ArenaFailureTestState g_arena_failure_state = {};

TEST_CASE("Arena creation rejects reserve and commit failures")
{
    for (ArenaFlags flags : {ArenaFlags(0), ArenaFlags(ArenaFlag_LargePages)})
    {
        ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(8), .flags = flags};
        g_arena_failure_state.fail_reserve = true;
        CHECK_THROWS_AS(arena_alloc(&params), ArenaFailureTestAbort);
        CHECK(g_arena_failure_state.mappings == 0);
        g_arena_failure_state.fail_reserve = false;

        g_arena_failure_state.fail_commit = true;
        CHECK_THROWS_AS(arena_alloc(&params), ArenaFailureTestAbort);
        CHECK(g_arena_failure_state.mappings == 0);
        g_arena_failure_state.fail_commit = false;
    }
}

TEST_CASE("Failed arena growth does not publish inaccessible bytes")
{
    for (ArenaFlags flags : {ArenaFlags(0), ArenaFlags(ArenaFlag_LargePages)})
    {
        ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(8), .flags = flags};
        Arena* arena = arena_alloc(&params);
        U64 old_pos = arena->pos;
        U64 old_commit = arena->cmt;
        g_arena_failure_state.fail_commit = true;
        CHECK_THROWS_AS(arena_push(arena, KB(16), 8), ArenaFailureTestAbort);
        CHECK(arena->pos == old_pos);
        CHECK(arena->cmt == old_commit);
        g_arena_failure_state.fail_commit = false;
        U8* bytes = PushArray(arena, U8, KB(16));
        CHECK(bytes[KB(16) - 1] == 0);
        CHECK(arena->cmt > old_commit);
        arena_release(arena);
        CHECK(g_arena_failure_state.mappings == 0);
    }
}

TEST_CASE("Failed chain creation leaves the original arena intact")
{
    ArenaParams params = {.reserve_size = KB(16), .commit_size = KB(4)};
    Arena* arena = arena_alloc(&params);
    U64 old_pos = arena->pos;
    g_arena_failure_state.fail_commit = true;
    CHECK_THROWS_AS(arena_push(arena, KB(32), 8), ArenaFailureTestAbort);
    CHECK(arena->current == arena);
    CHECK(arena->pos == old_pos);
    CHECK(g_arena_failure_state.mappings == 1);
    g_arena_failure_state.fail_commit = false;
    arena_release(arena);
}

TEST_CASE("Failed destructor commitment leaves its storage unpublished")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    {
        Allocator allocator(params);
        CHECK((allocator.arena->flags & ArenaFlag_NoChain) == 0);
        U64 old_destructor_pos = allocator.arena->destructor_pos;
        g_arena_failure_state.fail_commit = true;
        CHECK_THROWS_AS(allocator.make<ArenaFailureTestObject>(), ArenaFailureTestAbort);
        CHECK(allocator.arena->destructor_pos == old_destructor_pos);
        g_arena_failure_state.fail_commit = false;
        CHECK_NOTHROW(allocator.make<ArenaFailureTestObject>());
    }
    CHECK(g_arena_failure_state.mappings == 0);
}

ArenaFailureTestObject::~ArenaFailureTestObject() {}

// OS memory hooks for this executable only. Back mappings with accessible memory
// so a missing failure check is reported by the assertions rather than a crash.
OS_SystemInfo*
OS_GetSystemInfo()
{
    local_persist OS_SystemInfo system_info = {};
    system_info.page_size = KB(4);
    system_info.large_page_size = KB(8);
    return &system_info;
}

void*
os_reserve(U64 size)
{
    if (g_arena_failure_state.fail_reserve) return 0;
    void* memory = mi_malloc_aligned(size, KB(8));
    AssertAlways(memory);
    ++g_arena_failure_state.mappings;
    return memory;
}

B32
os_commit(void*, U64)
{
    return !g_arena_failure_state.fail_commit;
}

void
os_decommit(void*, U64) {}

void
os_release(void* ptr, U64 size)
{
    AsanUnpoisonMemoryRegion(ptr, size);
    mi_free(ptr);
    --g_arena_failure_state.mappings;
}

void*
os_reserve_large(U64 size)
{
    return os_reserve(size);
}

B32
os_commit_large(void* ptr, U64 size)
{
    return os_commit(ptr, size);
}

void
os_abort(S32 exit_code)
{
    CHECK(exit_code == 1);
    CHECK(g_arena_failure_state.stack_reports == 1);
    g_arena_failure_state.stack_reports = 0;
    throw ArenaFailureTestAbort{exit_code};
}

void
os_call_stack_print()
{
    ++g_arena_failure_state.stack_reports;
}
