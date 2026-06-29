
g_internal DebugEvent*
debug_event_record(DebugEventType event_type, String8 debug_name, String8 name)
{
    Assert((g_debug_table.arr_idx_and_event_idx.load() & 0xFFFFFFFF) < ArrayCount(g_debug_table.events[0]));
    U64 arr_idx_and_event_idx = g_debug_table.arr_idx_and_event_idx.fetch_add(1);
    U64 arr_idx_cur = arr_idx_and_event_idx >> 32;
    U64 event_idx = arr_idx_and_event_idx & 0xFFFFFFFF;
    DebugEvent* event = &g_debug_table.events[arr_idx_cur][event_idx];
    event->debug_name = debug_name;
    event->name = name;
    event->type = event_type;
    event->tid = os_tid();
    return event;
}

g_internal void
debug_dir_create(String8 debug_dir)
{
    if (!os_file_path_exists(debug_dir) && !os_make_directory(debug_dir))
    {
        ERROR_LOG("Could not create debug directory: %.*s\n", str8_varg(debug_dir));
        return;
    }
}

g_internal void
debug_g_state_init()
{
    g_debug_table.frame_arena = arena_alloc();
    Debug_SetName(g_debug_table.frame_arena, "debug frame arena");
    g_debug_table.arena = arena_alloc();
    Debug_SetName(g_debug_table.arena, "debug persistent arena");
    // setup debug path
    String8 debug_dir = S("debug");
    os_clear_directory(debug_dir);
    debug_dir_create(debug_dir);
}

g_internal void
debug_dump_str(String8 str, String8 filepath)
{
    if (str.size == 0)
    {
        return;
    }
    if (!os_append_data_to_file_path(filepath, str, OS_AccessFlag_ShareRead | OS_AccessFlag_ShareWrite))
    {
        ERROR_LOG("Could not write debug asset events to log: %.*s\n", str8_varg(filepath));
    }
}

g_internal void
_debug_arena_create_event(DebugEvent* debug_event)
{
    Assert(debug_event->type == DebugEventType::ArenaCreate);
    AllocationEvent* alloc_event = &debug_event->alloc_event;

    DebugArena* debug_arena = g_debug_table.debug_arena_first;
    for (; debug_arena; debug_arena = debug_arena->next)
    {
        if (OS_HandleMatch(debug_arena->arena_id, alloc_event->arena_block_handle))
        {
            break;
        }
    }

    if (!debug_arena)
    {
        debug_arena = g_debug_table.debug_arena_free_list;
        if (debug_arena)
        {
            SLLStackPop(g_debug_table.debug_arena_free_list);
            MemoryZeroStruct(debug_arena);
        }
        else
        {
            debug_arena = PushStruct(g_debug_table.arena, DebugArena);
        }

        DLLPushBack(g_debug_table.debug_arena_first, g_debug_table.debug_arena_last, debug_arena);
        debug_arena->arena_id = alloc_event->arena_block_handle;
    }

    debug_arena->total_memory_in_use = alloc_event->size;
    debug_arena->max_memory_used = Max(debug_arena->max_memory_used, debug_arena->total_memory_in_use);
    debug_arena->name = "<unnamed>";
}

g_internal void
_debug_arena_page_release_event(DebugEvent* debug_event)
{
    Assert(debug_event->type == DebugEventType::ArenaPageRelease);
    AllocationEvent* alloc_event = &debug_event->alloc_event;

    DebugArena* debug_arena = g_debug_table.debug_arena_first;
    for (; debug_arena; debug_arena = debug_arena->next)
    {
        if (OS_HandleMatch(debug_arena->arena_id, alloc_event->arena_block_handle))
        {
            break;
        }
    }
    Assert(debug_arena);

    debug_arena->total_memory_in_use = alloc_event->size;
}

g_internal void
_debug_arena_page_allocation_event(DebugEvent* debug_event)
{
    Assert(debug_event->type == DebugEventType::ArenaPageAllocation);
    AllocationEvent* alloc_event = &debug_event->alloc_event;
    DebugArena* debug_arena = g_debug_table.debug_arena_first;

    for (; debug_arena; debug_arena = debug_arena->next)
    {
        if (OS_HandleMatch(debug_arena->arena_id, alloc_event->arena_block_handle))
        {
            break;
        }
    }

    if (!debug_arena)
    {
        debug_arena = g_debug_table.debug_arena_free_list;
        if (debug_arena)
        {
            SLLStackPop(g_debug_table.debug_arena_free_list);
            MemoryZeroStruct(debug_arena);
        }
        else
        {
            debug_arena = PushStruct(g_debug_table.arena, DebugArena);
        }

        DLLPushBack(g_debug_table.debug_arena_first, g_debug_table.debug_arena_last, debug_arena);
        debug_arena->arena_id = alloc_event->arena_block_handle;
    }

    debug_arena->total_memory_in_use = alloc_event->size;
    debug_arena->max_memory_used = Max(debug_arena->max_memory_used, debug_arena->total_memory_in_use);
}

g_internal void
_debug_arena_release_event(DebugEvent* event)
{
    Assert(event->type == DebugEventType::ArenaRelease);
    OS_Handle arena_handle = event->handle;

    DebugArena* arena_to_release = 0;
    for (DebugArena* debug_arena = g_debug_table.debug_arena_first; debug_arena; debug_arena = debug_arena->next)
    {
        if (OS_HandleMatch(debug_arena->arena_id, arena_handle))
        {
            arena_to_release = debug_arena;
            break;
        }
    }
    Assert(arena_to_release);
    DLLRemove(g_debug_table.debug_arena_first, g_debug_table.debug_arena_last, arena_to_release);
}

g_internal void
_debug_handle_name_set_event(DebugEvent* debug_event)
{
    Assert(debug_event->type == DebugEventType::HandleNameSet);
    HandleNameSetEvent* handle_name_set_event = &debug_event->handle_name_set_event;

    DebugArena* arena_to_name = 0;
    for (DebugArena* debug_arena = g_debug_table.debug_arena_first; debug_arena; debug_arena = debug_arena->next)
    {
        if (OS_HandleMatch(debug_arena->arena_id, handle_name_set_event->handle))
        {
            arena_to_name = debug_arena;
            break;
        }
    }
    Assert(arena_to_name);
    arena_to_name->name = handle_name_set_event->name;
}

g_internal void
debug_event_frame_end()
{
    g_debug_table.arr_idx_cur = !g_debug_table.arr_idx_cur;
    U64 arr_idx_and_event_idx_prev = g_debug_table.arr_idx_and_event_idx.exchange(g_debug_table.arr_idx_cur << 32);
    U32 arr_idx = arr_idx_and_event_idx_prev >> 32;
    U32 event_count = arr_idx_and_event_idx_prev & 0xFFFFFFFF;

    g_debug_table.frame_count++;
    // init and resetting of debug arean
    if (!g_debug_table.frame_arena)
    {
        debug_g_state_init();
    }
    arena_clear(g_debug_table.frame_arena);
    g_debug_table.asset_events = *chunk_list_create<DebugEvent>(g_debug_table.frame_arena, 5000);
    g_debug_table.http_events = *chunk_list_create<DebugEvent>(g_debug_table.frame_arena, 5000);

    DebugEvent* event_arr = g_debug_table.events[arr_idx];
    for (U32 idx = 0; idx < event_count; ++idx)
    {
        DebugEvent* event = &event_arr[idx];

        switch (event->type)
        {
            case DebugEventType::ArenaCreate:
            {
                _debug_arena_create_event(event);
            }
            break;
            case DebugEventType::ArenaPageAllocation:
            {
                _debug_arena_page_allocation_event(event);
            }
            break;
            case DebugEventType::ArenaPageRelease:
            {
                _debug_arena_page_release_event(event);
            }
            break;
            case DebugEventType::ArenaRelease:
            {
                _debug_arena_release_event(event);
            }
            break;
            case DebugEventType::HandleNameSet:
            {
                _debug_handle_name_set_event(event);
            }
            break;
            case DebugEventType::Asset:
            {
                DebugEvent* chunk_list_asset_item = chunk_list_get_next(g_debug_table.frame_arena, &g_debug_table.asset_events);
                *chunk_list_asset_item = *event;
            }
            break;
            case DebugEventType::Http:
            {
                DebugEvent* chunk_list_http_item = chunk_list_get_next(g_debug_table.frame_arena, &g_debug_table.http_events);
                *chunk_list_http_item = *event;
            }
            break;
            default:
            {
                ERROR_LOG("Unknown error type encountered with type %d", (S32)event->type);
            }
        }
    }

    // asset logs
    {
        String8List asset_list = {};
        for (DebugEvent& debug_asset_event : g_debug_table.asset_events)
        {
            AssetEvent* asset_event = &debug_asset_event.asset_event;
            String8 str = push_str8f(g_debug_table.frame_arena, "%llu\t%d\t%llu\t%.*s\t%u\n", asset_event->handle_u64, asset_event->handle_type, asset_event->gen_id,
                                     str8_varg(g_asset_debug_type_strings[(U32)asset_event->state]), debug_asset_event.tid);
            str8_list_push(g_debug_table.frame_arena, &asset_list, str);
        }
        String8 asset_frame_log = str8_list_join(g_debug_table.frame_arena, &asset_list, 0);
        debug_dump_str(asset_frame_log, S("debug/asset_logs.log"));
    }

    // http logs
    {
        String8List http_list = {};
        for (DebugEvent& debug_http_event : g_debug_table.http_events)
        {
            DebugHttpEvent* http_event = &debug_http_event.http_event;
            String8 str = push_str8f(g_debug_table.frame_arena, "%d\t%u\t%u\n", http_event->async_result, http_event->error_code, debug_http_event.tid);
            str8_list_push(g_debug_table.frame_arena, &http_list, str);
        }

        String8 http_frame_log = str8_list_join(g_debug_table.frame_arena, &http_list, 0);
        debug_dump_str(http_frame_log, S("debug/http_logs.log"));
    }
}

g_internal void
debug_memory_snapshot_dump()
{
    // Arena Log
    U64 total_memory_committed = 0;
    U64 arena_count = 0;
    for (DebugArena* debug_arena = g_debug_table.debug_arena_first; debug_arena; debug_arena = debug_arena->next)
    {
        total_memory_committed += debug_arena->total_memory_in_use;
        arena_count++;
        String8 name = str8_from_memory_size(g_debug_table.frame_arena, debug_arena->total_memory_in_use);
        DEBUG_LOG("Arena Name: %s, memory: %.*s", debug_arena->name, str8_varg(name));
    }
    DEBUG_LOG("Arena Count: Count: %llu, Total memory committed: %llu", arena_count, total_memory_committed);
}
