#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include "resource_paths.hpp"

static String8
simulator_resource_root_get(Arena* arena)
{
    OS_ProcessInfo* process_info = os_get_process_info();
    String8 packaged_fonts = str8_path_from_str8_list(arena, {process_info->binary_path, S("simulator"), S("fonts")});
    B32 packaged_resources_exist = os_folder_path_exists(packaged_fonts);
    String8 root = process_info->binary_path;
    if (!packaged_resources_exist)
    {
        // Development builds can still use the source tree's runtime files.
        root = str8_c_string(DTCITY_PROJECT_ROOT);
    }
    return root;
}

static String8
simulator_database_directory_get(Arena* arena)
{
    String8 root = simulator_resource_root_get(arena);
    String8 directory = str8_path_from_str8_list(arena, {root, S("simulator"), S("database")});
    return directory;
}
