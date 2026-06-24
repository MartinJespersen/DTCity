// third party header
#define DOCTEST_CONFIG_IMPLEMENT
#include "third_party/doctest/doctest.h"
#include "glm/glm.hpp"

// user header
#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include "async/segment_buffer.hpp"
#include "async/async_heap.hpp"

// user source
#include "base/base_inc.cpp"
#include "async/segment_buffer.cpp"
#include "async/async_heap.cpp"

// test files
#include "async/test_heap.cpp"
#include "base/test_allocator.cpp"
#include "base/test_container.cpp"
#include "base/test_strings.cpp"

int
App(int argc, char** argv)
{
    return doctest::Context(argc, argv).run();
}
