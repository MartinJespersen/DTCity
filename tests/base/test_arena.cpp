#include "../test_inc.hpp"

TEST_CASE("Scratch-style arena resets retain committed pages and zero new allocations")
{
    ArenaParams params = {.reserve_size = MB(2), .commit_size = KB(64), .flags = ArenaFlag_RetainCommitted};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    U64 start = arena_pos(arena);
    U8* first = PushArray(arena, U8, KB(200));
    MemorySet(first, 0xAB, KB(200));
    U64 committed = arena->cmt;
    REQUIRE(committed > params.commit_size);

    for (U32 iteration = 0; iteration < 32; ++iteration)
    {
        arena_pop_to(arena, start);
        CHECK(arena->cmt == committed);
#if ASAN_ENABLED
        int poisoned = __asan_address_is_poisoned(first);
        CHECK(poisoned != 0);
#endif
        U8* reused = PushArray(arena, U8, KB(200));
        CHECK(reused == first);
        CHECK(reused[0] == 0);
        CHECK(reused[KB(200) - 1] == 0);
        CHECK(arena->cmt == committed);
#if ASAN_ENABLED
        int writable = __asan_address_is_poisoned(reused);
        CHECK(writable == 0);
#endif
        reused[0] = 0xAB;
        reused[KB(200) - 1] = 0xAB;
    }
}

TEST_CASE("Ordinary arenas still decommit unused pages on reset")
{
    ArenaParams params = {.reserve_size = MB(2), .commit_size = KB(64)};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    U64 start = arena_pos(arena);
    U8* allocation = PushArray(arena, U8, KB(200));
    allocation[0] = 42;
    REQUIRE(arena->cmt > params.commit_size);
    arena_pop_to(arena, start);
    CHECK(arena->cmt == params.commit_size);
    CHECK(arena->pos == start);
}

TEST_CASE("Retained chain blocks are reused and trimmed within a budget")
{
    ArenaParams params = {.reserve_size = KB(256), .commit_size = KB(64), .flags = ArenaFlag_RetainCommitted};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    U8* live = PushArray(arena, U8, KB(200));
    live[0] = 42;
    U64 live_end = arena_pos(arena);
    U8* first = PushArray(arena, U8, KB(200));
    Arena* first_block = arena->current;
    U8* second = PushArray(arena, U8, KB(200));
    Arena* second_block = arena->current;
    REQUIRE(first_block != second_block);
    first[KB(200) - 1] = 11;
    second[KB(200) - 1] = 22;
    arena_trim(arena, 0);
    CHECK(first[KB(200) - 1] == 11);
    CHECK(second[KB(200) - 1] == 22);
    CHECK(arena->current == second_block);
    arena_pop_to(arena, live_end);
    CHECK(arena->current == arena);
    CHECK(arena->free_size == 2 * params.reserve_size);
#if ASAN_ENABLED
    int first_poisoned = __asan_address_is_poisoned(first);
    int second_poisoned = __asan_address_is_poisoned(second);
    int live_poisoned = __asan_address_is_poisoned(live);
    CHECK(first_poisoned != 0);
    CHECK(second_poisoned != 0);
    CHECK(live_poisoned == 0);
#endif

    U8* reused_first = PushArray(arena, U8, KB(200));
    U8* reused_second = PushArray(arena, U8, KB(200));
    CHECK(reused_first == first);
    CHECK(reused_second == second);
    CHECK(arena->free_last == nullptr);
    CHECK(arena->free_size == 0);
    reused_first[0] = 1;
    reused_second[0] = 2;

    arena_pop_to(arena, live_end);
    arena_trim(arena, KB(512));
    CHECK(arena->free_size == params.reserve_size);
    REQUIRE(arena->free_last != nullptr);
    CHECK(arena->cmt + arena->free_last->cmt <= KB(512));
    CHECK(live[0] == 42);
    CHECK(arena->pos == live_end);
    arena_trim(arena, 0);
    CHECK(arena->free_last == nullptr);
    CHECK(arena->free_size == 0);
    CHECK(live[0] == 42);
}

TEST_CASE("Trimming preserves live data and releases unused commitment")
{
    ArenaParams params = {.reserve_size = MB(2), .commit_size = KB(64), .flags = ArenaFlag_RetainCommitted};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    U8* live = PushArray(arena, U8, KB(100));
    live[KB(100) - 1] = 99;
    U64 live_end = arena_pos(arena);
    U8* temporary = PushArray(arena, U8, KB(600));
    temporary[0] = 1;
    arena_pop_to(arena, live_end);
    REQUIRE(arena->cmt > KB(256));
    arena_trim(arena, KB(256));
    CHECK(arena->cmt == KB(256));
    CHECK(arena->pos == live_end);
    CHECK(live[KB(100) - 1] == 99);
    arena_trim(arena, 0);
    CHECK(arena->cmt == KB(128));
    CHECK(live[KB(100) - 1] == 99);
    arena_clear(arena);
    arena_trim(arena, 0);
    CHECK(arena->cmt == params.commit_size);
}

TEST_CASE("Cached blocks account for alignment and header size")
{
    ArenaParams params = {.reserve_size = KB(64), .commit_size = KB(4), .flags = ArenaFlag_RetainCommitted};
    Arena* arena = arena_alloc(&params);
    defer(arena_release(arena));
    U8* first = PushArray(arena, U8, KB(60));
    U64 live_end = arena_pos(arena);
    U8* temporary = PushArray(arena, U8, KB(60));
    temporary[0] = 1;
    arena_pop_to(arena, live_end);
    REQUIRE(arena->free_last != nullptr);
    Arena* cached = arena->free_last;
    U8* aligned = (U8*)arena_push(arena, KB(64) - ARENA_HEADER_SIZE, KB(4));
    CHECK(arena->current != cached);
    CHECK((U64)aligned % KB(4) == 0);
    aligned[KB(64) - ARENA_HEADER_SIZE - 1] = 7;
    first[0] = 3;
    CHECK(first[0] == 3);
}
