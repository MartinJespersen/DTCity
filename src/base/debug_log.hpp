#pragma once

#include "os_core/os_core_inc.hpp"

#if BUILD_DEBUG
// Asset Debug Types ///////////////////
#define Debug_Asset_Type_X_List(X)                                                                                                                                                                     \
    X(Created)                                                                                                                                                                                         \
    X(Queued)                                                                                                                                                                                          \
    X(GpuSubmitted)                                                                                                                                                                                    \
    X(ScheduleDeletion)                                                                                                                                                                                \
    X(Deletion)                                                                                                                                                                                        \
    X(NotFound)                                                                                                                                                                                        \
    X(GpuSubmissionDone)                                                                                                                                                                               \
    X(WrongHandleType)                                                                                                                                                                                 \
    X(WrongGenId)

enum class DebugAssetType : S32
{
#define Debug_Asset_Type_Enum_Entry(name) name,
    Debug_Asset_Type_X_List(Debug_Asset_Type_Enum_Entry)
#undef Debug_Asset_Type_Enum_Entry
};

const String8 g_asset_debug_type_strings[] = {
#define Debug_Asset_Type_String_Entry(name) str8_lit_comp(#name),
    Debug_Asset_Type_X_List(Debug_Asset_Type_String_Entry)
#undef Debug_Asset_Type_String_Entry
};

struct AssetEvent
{
    DebugAssetType state;
    U64 handle_u64;
    U64 gen_id;
    S32 handle_type;
};

/////////////////////////////////////
struct AllocationEvent
{
    OS_Handle arena_handle;
    U32 cmt_size;
};

// HTTP debugging /////////////////////
struct DebugHttpEvent
{
    S32 async_result;
    U32 error_code;
};
///////////////////////////////////////
enum class DebugEventType : S32
{
    Allocation,
    Cesium_TileLoad,
    Cesium_TileUnload,
    Asset,
    Http

};

struct DebugEvent
{
    String8 debug_name; // should always be created from string literal from macro
    String8 name;       // should always be created from string literal from macro
    U32 tid;

    DebugEventType type;
    union
    {
        AllocationEvent alloc_event;
        AssetEvent asset_event;
        DebugHttpEvent http_event;
    };
};

struct DebugTable
{
    Arena* arena;
    U64 arr_idx_cur;
    std::atomic<U64> arr_idx_and_event_idx; // msb 32 bit: event array index. lsb 32 bits: event idx
    // we switch between two buffers so that other threads can access the one not used by the reader
    DebugEvent events[2][16 * 65536];

    U64 total_memory_use;
    ChunkList<DebugEvent> asset_events;
    ChunkList<DebugEvent> http_events;
};

#define Debug_Name_(file, line, label) S(file ":" Stringify(line) "|" label)
#define Debug_Name(label) Debug_Name_(__FILE__, __LINE__, label)

DebugTable g_debug_table = {};

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

#define Debug_Asset_Push_(asset_state, handle)                                                                                                                                                         \
    {                                                                                                                                                                                                  \
        auto debug_asset_handle__ = (handle);                                                                                                                                                          \
        DebugEvent* event = debug_event_record(DebugEventType::Asset, Debug_Name("Asset Push " #asset_state), S(#asset_state));                                                                        \
        event->asset_event.state = DebugAssetType::asset_state;                                                                                                                                        \
        event->asset_event.handle_u64 = debug_asset_handle__.u64;                                                                                                                                      \
        event->asset_event.gen_id = debug_asset_handle__.gen_id;                                                                                                                                       \
        event->asset_event.handle_type = (S32)debug_asset_handle__.type;                                                                                                                               \
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
    g_debug_table.arena = arena_alloc();
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
debug_event_frame_end()
{
    g_debug_table.arr_idx_cur = !g_debug_table.arr_idx_cur;
    U64 arr_idx_and_event_idx_prev = g_debug_table.arr_idx_and_event_idx.exchange(g_debug_table.arr_idx_cur << 32);
    U32 arr_idx = arr_idx_and_event_idx_prev >> 32;
    U32 event_count = arr_idx_and_event_idx_prev & 0xFFFFFFFF;

    // init and resetting of debug arean
    if (!g_debug_table.arena)
    {
        debug_g_state_init();
    }
    arena_clear(g_debug_table.arena);
    g_debug_table.asset_events = *chunk_list_create<DebugEvent>(g_debug_table.arena, 5000);
    g_debug_table.http_events = *chunk_list_create<DebugEvent>(g_debug_table.arena, 5000);

    DebugEvent* event_arr = g_debug_table.events[arr_idx];
    for (U32 idx = 0; idx < event_count; ++idx)
    {
        DebugEvent* event = &event_arr[idx];

        switch (event->type)
        {
            case DebugEventType::Allocation:
            {
            }
            break;
            case DebugEventType::Asset:
            {
                DebugEvent* chunk_list_asset_item = chunk_list_get_next(g_debug_table.arena, &g_debug_table.asset_events);
                *chunk_list_asset_item = *event;
            }
            break;
            case DebugEventType::Http:
            {
                DebugEvent* chunk_list_http_item = chunk_list_get_next(g_debug_table.arena, &g_debug_table.http_events);
                *chunk_list_http_item = *event;
            }
            break;
            default:
            {
                ERROR_LOG("Unknown error type encountered with type %d", (S32)event->type);
            }
        }
    }

    String8List asset_list = {};
    for (DebugEvent& debug_asset_event : g_debug_table.asset_events)
    {
        AssetEvent* asset_event = &debug_asset_event.asset_event;
        String8 str = push_str8f(g_debug_table.arena, "%llu\t%d\t%llu\t%.*s\t%u\n", asset_event->handle_u64, asset_event->handle_type, asset_event->gen_id,
                                 str8_varg(g_asset_debug_type_strings[(U32)asset_event->state]), debug_asset_event.tid);
        str8_list_push(g_debug_table.arena, &asset_list, str);
    }

    String8 asset_frame_log = str8_list_join(g_debug_table.arena, &asset_list, 0);
    debug_dump_str(asset_frame_log, S("debug/asset_logs.log"));

    String8List http_list = {};
    for (DebugEvent& debug_http_event : g_debug_table.http_events)
    {
        DebugHttpEvent* http_event = &debug_http_event.http_event;
        String8 str = push_str8f(g_debug_table.arena, "%d\t%u\t%u\n", http_event->async_result, http_event->error_code, debug_http_event.tid);
        str8_list_push(g_debug_table.arena, &http_list, str);
    }

    String8 http_frame_log = str8_list_join(g_debug_table.arena, &http_list, 0);
    debug_dump_str(http_frame_log, S("debug/http_logs.log"));
}

#define Debug_Frame_End() debug_event_frame_end()

// Arena debugging macros
#define Debug_Allocation_Push(arena, size)                                                                                                                                                             \
    {                                                                                                                                                                                                  \
        DebugEvent* event = debug_event_record(DebugEventType::Allocation, S("Arena Allocation Push"), S("Arena Allocation Push"));                                                                    \
        event->alloc_event.arena_handle = OS_HandleFromPtr(arena);                                                                                                                                     \
        event->alloc_event.cmt_size = (size);                                                                                                                                                          \
    }
/////////////////////////////////////////
// Http debugging macros
#define Debug_Http_Push(http_result)                                                                                                                                                                   \
    {                                                                                                                                                                                                  \
        auto debug_http_result__ = (http_result);                                                                                                                                                      \
        DebugEvent* event = debug_event_record(DebugEventType::Http, Debug_Name("Http Push"), S("Http Push"));                                                                                         \
        event->http_event.async_result = (S32)debug_http_result__.result;                                                                                                                              \
        event->http_event.error_code = debug_http_result__.curl_code;                                                                                                                                  \
    }
////////////////////////////////////////

#define Debug_Asset_Push_Created(handle) Debug_Asset_Push_(Created, handle)
#define Debug_Asset_Push_Queued(handle) Debug_Asset_Push_(Queued, handle)
#define Debug_Asset_Push_GpuSubmitted(handle) Debug_Asset_Push_(GpuSubmitted, handle)
#define Debug_Asset_Push_GpuSubmissionDone(handle) Debug_Asset_Push_(GpuSubmissionDone, handle)
#define Debug_Asset_Push_ScheduleDeletion(handle) Debug_Asset_Push_(ScheduleDeletion, handle)
#define Debug_Asset_Push_Deletion(handle) Debug_Asset_Push_(Deletion, handle)
#define Debug_Asset_Push_NotFound(handle) Debug_Asset_Push_(NotFound, handle)
#define Debug_Asset_Push_WrongHandleType(handle) Debug_Asset_Push_(WrongHandleType, handle)
#define Debug_Asset_Push_WrongGenId(handle) Debug_Asset_Push_(WrongGenId, handle)

#else
#define Debug_Allocation_Push(...)
#define Debug_Frame_End(...)
#define Debug_Http_Push(...)
#define Debug_Asset_Push_(...)
#define Debug_Asset_Push_Created(...)
#define Debug_Asset_Push_Queued(...)
#define Debug_Asset_Push_GpuSubmitted(...)
#define Debug_Asset_Push_ScheduleDeletion(...)
#define Debug_Asset_Push_Deletion(...)
#define Debug_Asset_Push_GpuSubmissionDone(...)
#define Debug_Asset_Push_NotFound(...)
#define Debug_Asset_Push_WrongHandleType(...)
#define Debug_Asset_Push_WrongGenId(...)
#endif
