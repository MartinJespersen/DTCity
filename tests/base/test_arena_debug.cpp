#define DOCTEST_CONFIG_IMPLEMENT
#include "third_party/doctest/doctest.h"
#include "debug_log.hpp"
#include "base/base_inc.hpp"
#include "base/base_allocator_templates.hpp"
#include "base/container_templates.hpp"
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>

// Exercise the same hooks as the application, including startup before debug-state initialization.
#include "debug_log.cpp"
#include "base/base_inc.cpp"

TEST_CASE("Arena creation sites capture the caller of both allocation overloads")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    U32 explicit_line = __LINE__ + 1;
    Arena* explicit_arena = arena_alloc(&params);
    defer(arena_release(explicit_arena));
    U32 default_line = __LINE__ + 1;
    Arena* default_arena = arena_alloc();
    defer(arena_release(default_arena));
    Arena* samples = arena_alloc();
    defer(arena_release(samples));

    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    for (U64 index = 0; index < 2; ++index)
    {
        U64 arena_id = IntFromPtr(index == 0 ? explicit_arena : default_arena);
        auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
        REQUIRE(row != rows.end());
        REQUIRE(row->source_file != nullptr);
        CHECK(std::string(row->source_file) == __FILE__);
        CHECK(row->source_line == (index == 0 ? explicit_line : default_line));
    }
}

TEST_CASE("The original arena allocation functions remain available for callbacks")
{
    Arena* (*allocate)(ArenaParams*) = arena_alloc;
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    Arena* arena = allocate(&params);
    defer(arena_release(arena));
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    U64 arena_id = IntFromPtr(arena);
    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
    REQUIRE(row != rows.end());
    CHECK(row->source_file == nullptr);
    CHECK(row->source_line == 0);
    CHECK(row->metrics.reserved == params.reserve_size);
}

TEST_CASE("Arena samples distinguish live bytes, commitment, and reservation")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    U64 arena_id = IntFromPtr(arena);
    char name[] = "test arena";
    Debug_SetName(arena, name);
    name[0] = 'X';

    for (U32 step = 0; step < 3; ++step)
    {
        if (step == 1) arena_push(arena, KB(12), KB(4));
        if (step == 2) arena_clear(arena);
        arena_clear(samples);
        Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
        auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
        REQUIRE(row != rows.end());
        CHECK(std::string(row->name) == "test arena");
        CHECK(row->metrics.used == arena->pos);
        CHECK(row->metrics.committed == arena->cmt);
        CHECK(row->metrics.reserved == params.reserve_size);
        CHECK(row->metrics.block_count == 1);
    }
    CHECK(arena->cmt == params.commit_size);
}

TEST_CASE("Arena samples group retained and reused chain blocks without base-position gaps")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4), .flags = ArenaFlag_RetainCommitted};
    U32 source_line = __LINE__ + 1;
    Arena* arena = arena_alloc(&params);
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    U64 arena_id = IntFromPtr(arena);
    Debug_SetName(arena, "chain owner");
    arena_push(arena, KB(8), 8);
    U64 first_end = arena_pos(arena);

    for (U32 step = 0; step < 4; ++step)
    {
        if (step == 0 || step == 2) arena_push(arena, KB(60), 8);
        if (step == 1) arena_pop_to(arena, first_end);
        if (step == 3)
        {
            arena_pop_to(arena, first_end);
            arena_trim(arena, 0);
        }
        arena_clear(samples);
        Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
        auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
        REQUIRE(row != rows.end());
        CHECK(std::string(row->name) == "chain owner");
        REQUIRE(row->source_file != nullptr);
        CHECK(std::string(row->source_file) == __FILE__);
        CHECK(row->source_line == source_line);
        CHECK(row->metrics.block_count == (step == 3 ? 1 : 2));
        U64 used = arena->pos;
        U64 committed = arena->cmt;
        U64 reserved = arena->res;
        Arena* child = step == 1 ? arena->free_last : arena->current;
        if (child != arena && child)
        {
            used += child->pos;
            committed += child->cmt;
            reserved += child->res;
        }
        CHECK(row->metrics.used == used);
        CHECK(row->metrics.committed == committed);
        CHECK(row->metrics.reserved == reserved);
    }

    arena_release(arena);
    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    CHECK(std::none_of(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; }));
}

TEST_CASE("Released chain blocks and arenas created between samples disappear")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    U64 arena_id = IntFromPtr(arena);
    arena_push(arena, KB(60), 8);
    arena_push(arena, KB(60), 8);
    arena_clear(arena);
    for (U32 index = 0; index < 32; ++index)
    {
        Arena* temporary = arena_alloc(&params);
        arena_release(temporary);
    }
    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
    REQUIRE(row != rows.end());
    CHECK(row->metrics.block_count == 1);
    CHECK(row->metrics.used == ARENA_HEADER_SIZE);
    CHECK(row->metrics.committed == params.commit_size);
    CHECK(row->metrics.reserved == params.reserve_size);
}

TEST_CASE("Allocator samples include destructor bytes and disjoint committed pages")
{
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    SUBCASE("Disjoint front and destructor pages") {}
    SUBCASE("Overlapping committed regions are counted once") { params.commit_size = params.reserve_size; }
    Allocator allocator(params);
    U64 arena_id = IntFromPtr(allocator.arena);
    allocator.make<std::string>("destructor record");
    for (U32 step = 0; step < 2; ++step)
    {
        if (step == 1) allocator.clear();
        Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
        auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
        REQUIRE(row != rows.end());
        U64 destructor_used = allocator.arena->res - allocator.arena->destructor_pos;
        U64 committed = Min(params.reserve_size, params.commit_size + OS_GetSystemInfo()->page_size);
        CHECK(row->metrics.used == allocator.arena->pos + destructor_used);
        CHECK(row->metrics.committed == committed);
        CHECK(row->metrics.reserved == params.reserve_size);
        CHECK((destructor_used != 0) == (step == 0));
    }
}

TEST_CASE("Arena sampling does not read released mappings while workers allocate and reset")
{
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    std::atomic<U32> completed = 0;
    std::thread workers[4];
    for (std::thread& worker : workers)
    {
        worker = std::thread([&completed] {
            ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4), .flags = ArenaFlag_RetainCommitted};
            for (U32 index = 0; index < 500; ++index)
            {
                Arena* arena = arena_alloc(&params);
                Debug_SetName(arena, "concurrent arena");
                arena_push(arena, KB(8), 8);
                arena_push(arena, KB(60), 8);
                arena_clear(arena);
                arena_trim(arena, 0);
                arena_release(arena);
            }
            completed.fetch_add(1, std::memory_order_release);
        });
    }
    do
    {
        arena_clear(samples);
        Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
        for (DebugArenaSnapshot& row : rows)
        {
            CHECK(row.metrics.used <= row.metrics.committed);
            CHECK(row.metrics.committed <= row.metrics.reserved);
            CHECK(row.metrics.block_count > 0);
        }
    } while (completed.load(std::memory_order_acquire) != ArrayCount(workers));
    for (std::thread& worker : workers) worker.join();
    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    for (DebugArenaSnapshot& row : rows) CHECK(std::string(row.name) != "concurrent arena");
}

TEST_CASE("Arena samples remove destructor commitment when overlapping front pages are trimmed")
{
    Arena* samples = arena_alloc();
    defer(arena_release(samples));
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4)};
    Allocator allocator(params);
    U64 arena_id = IntFromPtr(allocator.arena);
    allocator.make<std::string>("destructor record");
    U64 remaining = allocator.arena->destructor_pos - allocator.arena->pos - 8;
    arena_push(allocator.arena, remaining, 8);
    REQUIRE(allocator.arena->cmt == params.reserve_size);
    allocator.clear();
    Buffer<DebugArenaSnapshot> rows = debug_arena_snapshot(samples);
    auto row = std::find_if(rows.begin(), rows.end(), [arena_id](auto& item) { return item.arena_id == arena_id; });
    REQUIRE(row != rows.end());
    CHECK(row->metrics.used == ARENA_HEADER_SIZE);
    CHECK(row->metrics.committed == params.commit_size);
    CHECK(row->metrics.reserved == params.reserve_size);
}

int
App(int argc, char** argv)
{
    return doctest::Context(argc, argv).run();
}
