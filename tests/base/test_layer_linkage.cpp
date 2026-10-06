#include "layer_linkage_inc.hpp"

TEST_CASE("base state and templates are shared across translation units")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    TCTX* thread_context = TCTX_Get();
    U64 saved_reserve_size = arena_default_reserve_size;
    defer(arena_default_reserve_size = saved_reserve_size);
    arena_default_reserve_size = MB(8);

    TestLayerLinkageProbe probe = {};
    test_layer_linkage_probe(arena, &probe);

    CHECK(probe.thread_context == thread_context);
    CHECK(probe.scratch_arena != arena);
    CHECK(probe.default_reserve_size == MB(8));
    REQUIRE(probe.values.size == 2);
    CHECK(probe.values.data[0] == 17);
    CHECK(probe.values.data[1] == 29);
}
