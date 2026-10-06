#include "test_inc.hpp"

// user source
#include "base/base_inc.cpp"
#include "misc/geometry.cpp"
#include "../simulator/resource_paths.cpp"
#include "../simulator/event_snapshot.cpp"
#include "city/simulator_shared_interface.cpp"
#include "city/simulator_messages.cpp"
#include "city/agent_state.cpp"
#include "../simulator/metadata.cpp"

// test files
#include "async/test_heap.cpp"
#include "base/test_allocator.cpp"
#include "base/test_layer_linkage.cpp"
#include "base/test_arena.cpp"
#include "base/test_container.cpp"
#include "base/test_math.cpp"
#include "base/test_strings.cpp"
#include "cesium/test_tessellation.cpp"
#include "misc/test_geometry.cpp"
#include "simulator/test_resource_paths.cpp"
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
