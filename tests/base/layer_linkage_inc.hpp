#pragma once

#include "diagnostics.hpp"
#include "base/base_inc.hpp"

struct TestLayerLinkageProbe
{
    TCTX* thread_context;
    Arena* scratch_arena;
    U64 default_reserve_size;
    Buffer<U32> values;
};

void
test_layer_linkage_probe(Arena* arena, TestLayerLinkageProbe* probe);
