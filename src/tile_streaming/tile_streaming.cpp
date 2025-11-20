// headers
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "gltfw/gltfw.hpp"

// sources
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "gltfw/gltfw.cpp"

void
App(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    Arena* arena = ArenaAlloc();
    String8 file_name = S("../data/a.glb");

    gltfw_Result result = gltfw_glb_read(arena, file_name);
}
