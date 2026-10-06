#pragma once

#include <string>
#include <vector>
// third party header
#define DOCTEST_CONFIG_IMPLEMENT
#include "third_party/doctest/doctest.h"
#include "glm/glm.hpp"
#include <limits>

struct alignas(64) DynamicArrayAlignedTestItem
{
    unsigned long long value;
};

// user header
#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include "base/test_allocator.hpp"
#include "async/segment_buffer.hpp"
#include "async/async_heap.hpp"
#include "async/segment_buffer_templates.hpp"
#include "async/async_heap_templates.hpp"
#include "async/thread_pool.hpp"
#include "render/render.hpp"
#include "misc/geometry.hpp"
#include "../simulator/resource_paths.hpp"
#include "../simulator/third_party/sqlite/sqlite3.h"
#include "../simulator/third_party/yyjson/yyjson.h"
#include "../simulator/event_snapshot.hpp"
#include "../simulator/metadata.hpp"
#include "third_party/simdjson/simdjson.h"
#include "city/simulator_shared_interface.hpp"
#include "city/simulator_messages.hpp"
namespace ui
{
struct Camera;
}
namespace cesium
{
struct TilesetRenderer;
}
#include "city/agent.hpp"

#if ASAN_ENABLED
C_LINKAGE int
__asan_address_is_poisoned(void const volatile* address);
#endif
