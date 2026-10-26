#define DOCTEST_CONFIG_IMPLEMENT
#include "test_inc.hpp"

// user source
#include "base/base_inc.cpp"
#include "misc/geometry.cpp"
#include "../simulator/resource_paths.cpp"
#include "../simulator/scenarios.cpp"
#include "../simulator/event_snapshot.cpp"
#include "city/simulator_shared_interface.cpp"
#include "city/simulator_messages.cpp"
#include "city/agent_state.cpp"
#include "city/agent_height.cpp"
#include "render/tile_bvh.cpp"
#include "city/tile_bvh.cpp"
#include "city/tile_height.cpp"
#include "render/tile_bvh_test_backend.cpp"
#include "../simulator/metadata.cpp"

// test files
#include "async/async_error.cpp"
#include "async/test_error.cpp"
#include "async/test_heap.cpp"
#include "base/test_allocator.cpp"
#include "base/test_cache.cpp"
#include "async/test_task_lifetime.cpp"
#include "async/test_http.cpp"
#include "base/test_layer_linkage.cpp"
#include "base/test_arena.cpp"
#include "base/test_container.cpp"
#include "base/test_math.cpp"
#include "base/test_mutex.cpp"
#include "base/test_strings.cpp"
#include "cesium/test_tessellation.cpp"
#include "cesium/test_tile_mesh_bvh.cpp"
#include "cesium/test_tile_bvh.cpp"
#include "cesium/test_tile_height.cpp"
#include "misc/test_geometry.cpp"
#include "simulator/test_resource_paths.cpp"
#include "simulator/test_scenarios.cpp"
#include "simulator/test_event_snapshot.cpp"
#include "simulator/test_messages.cpp"
#include "simulator/test_agent_state.cpp"
#include "simulator/test_metadata.cpp"
#include "base/test_freelist.cpp"

int
App(int argc, char** argv)
{
    return doctest::Context(argc, argv).run();
}
