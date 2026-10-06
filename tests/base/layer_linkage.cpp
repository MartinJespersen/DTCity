#include "layer_linkage_inc.hpp"

// Compiled separately from test_main.cpp to exercise the real linkage boundary.
void
test_layer_linkage_probe(Arena* arena, TestLayerLinkageProbe* probe)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    probe->thread_context = TCTX_Get();
    probe->scratch_arena = scratch.arena;
    probe->default_reserve_size = arena_default_reserve_size;
    probe->values = buffer_alloc<U32>(arena, 2);
    probe->values.data[0] = 17;
    probe->values.data[1] = 29;
}
