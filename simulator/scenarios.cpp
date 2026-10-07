static Buffer<SimulatorScenario>
simulator_scenarios_find(Arena* arena, String8 directory)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    B32 directory_exists = os_folder_path_exists(directory);
    if (!directory_exists) return {};

    // Discover files once and retain their exact paths for scenario selection.
    String8List filenames = {};
    OS_FileIter* iter = os_file_iter_begin(scratch.arena, directory, OS_FileIterFlag_SkipFolders);
    defer(os_file_iter_end(iter));
    OS_FileInfo info = {};
    for (;;)
    {
        B32 found = os_file_iter_next(scratch.arena, iter, &info);
        if (!found) break;
        String8 extension = str8_skip_last_dot(info.name);
        B32 sqlite_file = str8_match(extension, S("sqlite"), MatchFlag_CaseInsensitive);
        B32 sqlite3_file = str8_match(extension, S("sqlite3"), MatchFlag_CaseInsensitive);
        B32 db_file = str8_match(extension, S("db"), MatchFlag_CaseInsensitive);
        String8 name = str8_chop_last_dot(info.name);
        if ((sqlite_file || sqlite3_file || db_file) && name.size && name.size < info.name.size)
        {
            str8_list_push(scratch.arena, &filenames, info.name);
        }
    }

    Buffer<SimulatorScenario> scenarios = buffer_alloc<SimulatorScenario>(arena, filenames.node_count);
    U64 index = 0;
    for (String8Node* filename = filenames.first; filename; filename = filename->next)
    {
        String8 name = str8_chop_last_dot(filename->string);
        scenarios.data[index].name = push_str8_copy(arena, name);
        scenarios.data[index].path = str8_path_from_str8_list(arena, {directory, filename->string});
        ++index;
    }
    return scenarios;
}

static String8
simulator_scenario_path_find(Arena* arena, String8 directory, String8 name)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<SimulatorScenario> scenarios = simulator_scenarios_find(scratch.arena, directory);
    String8 path = {};
    for (U64 index = 0; index < scenarios.size; ++index)
    {
        B32 matches = str8_match(scenarios.data[index].name, name, 0);
        if (matches)
        {
            path = push_str8_copy(arena, scenarios.data[index].path);
            break;
        }
    }
    return path;
}
