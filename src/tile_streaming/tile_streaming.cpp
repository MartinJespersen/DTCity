// headers
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"

// sources
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"

void
App(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    Arena* arena = ArenaAlloc();
    String8 file_name = S("../data/a.glb");
    OS_Handle file = os_file_open(OS_AccessFlag_Read, file_name);
    FileProperties file_props = os_properties_from_file(file);

    String8 glb_file = push_str8_fill_byte(arena, file_props.size, 0);

    Rng1U64 rng = {0, file_props.size};
    U64 bytes_read = os_file_read(file, rng, glb_file.str);

    if (bytes_read != file_props.size)
    {
        exit_with_error("Failed to read file");
    }
}
