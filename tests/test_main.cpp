#include "test_inc.hpp"

// user source
#include "base/base_inc.cpp"
#include "async/segment_buffer.cpp"
#include "async/async_heap.cpp"
#include "misc/geometry.cpp"
#include "../simulator/resource_paths.cpp"

// test files
#include "async/test_heap.cpp"
#include "base/test_allocator.cpp"
#include "base/test_arena.cpp"
#include "base/test_container.cpp"
#include "base/test_math.cpp"
#include "base/test_strings.cpp"
#include "cesium/test_tessellation.cpp"
#include "misc/test_geometry.cpp"
#include "simulator/test_resource_paths.cpp"
#include "base/test_freelist.cpp"

int
App(int argc, char** argv)
{
    return doctest::Context(argc, argv).run();
}
