#pragma once

#include "os_core/os_core_inc.hpp"

#if BUILD_DEBUG

// Asset Debug Types ///////////////////
#define Debug_Asset_Type_X_List(X) \
    X(Created)                     \
    X(Queued)                      \
    X(GpuSubmitted)                \
    X(ScheduleDeletion)            \
    X(Deletion)                    \
    X(NotFound)                    \
    X(GpuSubmissionDone)           \
    X(WrongHandleType)             \
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
    OS_Handle arena_block_handle;
    U64 size;
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
    HandleNameSet,
    ArenaCreate,
    ArenaPageAllocation,
    ArenaPageRelease,
    ArenaRelease,
    Cesium_TileLoad,
    Cesium_TileUnload,
    Asset,
    Http

};

struct DebugArena
{
    DebugArena* next;
    DebugArena* prev;
    DebugArena* arena_block_first;
    OS_Handle arena_id;
    const char* name;

    // metrics
    U64 total_memory_in_use;
    U64 max_memory_used;
};

struct HandleNameSetEvent
{
    OS_Handle handle;
    const char* name;
};

struct DebugEvent
{
    String8 debug_name; // should always be created from string literal from macro
    String8 name;       // should always be created from string literal from macro
    U32 tid;

    DebugEventType type;
    union
    {
        HandleNameSetEvent handle_name_set_event;
        OS_Handle handle;
        AllocationEvent alloc_event;
        AssetEvent asset_event;
        DebugHttpEvent http_event;
    };
};

struct DebugTable
{
    Arena* arena;
    Arena* frame_arena;
    U64 frame_count;
    U64 arr_idx_cur;
    std::atomic<U64> arr_idx_and_event_idx; // msb 32 bit: event array index. lsb 32 bits: event idx
    // we switch between two buffers so that other threads can access the one not used by the reader
    DebugEvent events[2][16 * 65536];

    U64 total_memory_use;
    ChunkList<DebugEvent> asset_events;
    ChunkList<DebugEvent> http_events;

    // memory debugging
    DebugArena* debug_arena_first;
    DebugArena* debug_arena_last;
    DebugArena* debug_arena_free_list;
};

#define Debug_Name_(file, line, label) S(file ":" Stringify(line) "|" label)
#define Debug_Name(label) Debug_Name_(__FILE__, __LINE__, label)

DebugTable g_debug_table = {};

g_internal DebugEvent*
debug_event_record(DebugEventType event_type, String8 debug_name, String8 name);

g_internal void
debug_dir_create(String8 debug_dir);

g_internal void
debug_g_state_init();

g_internal void
debug_dump_str(String8 str, String8 filepath);

g_internal void
debug_event_frame_end();

g_internal void
debug_memory_snapshot_dump();
g_internal void
_debug_arena_page_release_event(DebugEvent* debug_event);

g_internal void
_debug_arena_create_event(DebugEvent* debug_event);

g_internal void
_debug_arena_page_allocation_event(DebugEvent* debug_event);

g_internal void
_debug_arena_release_event(DebugEvent* debug_event);

g_internal void
_debug_handle_name_set_event(DebugEvent* debug_event);

#define Debug_Asset_Push_(asset_state, handle)                                                                                  \
    {                                                                                                                           \
        auto debug_asset_handle__ = (handle);                                                                                   \
        DebugEvent* event = debug_event_record(DebugEventType::Asset, Debug_Name("Asset Push " #asset_state), S(#asset_state)); \
        event->asset_event.state = DebugAssetType::asset_state;                                                                 \
        event->asset_event.handle_u64 = debug_asset_handle__.u64;                                                               \
        event->asset_event.gen_id = debug_asset_handle__.gen_id;                                                                \
        event->asset_event.handle_type = (S32)debug_asset_handle__.type;                                                        \
    }

#define Debug_Frame_End() debug_event_frame_end()
#define Debug_Memory_Snapshot_Dump() debug_memory_snapshot_dump()

// Arena debugging macros
#define Debug_ArenaCreate_Push(arena_block, s)                                                                     \
    {                                                                                                              \
        DebugEvent* event = debug_event_record(DebugEventType::ArenaCreate, S("Arena Create"), S("Arena Create")); \
        event->alloc_event.arena_block_handle = OS_HandleFromPtr(arena_block);                                     \
        event->alloc_event.size = (s);                                                                             \
    }
#define Debug_PageAllocation_Push(arena_block, s)                                                                                                      \
    {                                                                                                                                                  \
        DebugEvent* event = debug_event_record(DebugEventType::ArenaPageAllocation, S("Arena Page Allocation Push"), S("Arena Page Allocation Push")); \
        event->alloc_event.arena_block_handle = OS_HandleFromPtr(arena_block);                                                                         \
        event->alloc_event.size = (s);                                                                                                                 \
    }
#define Debug_PageRelease_Push(arena_block, s)                                                                                                \
    {                                                                                                                                         \
        DebugEvent* event = debug_event_record(DebugEventType::ArenaPageRelease, S("Arena Page Release Push"), S("Arena Page Release Push")); \
        event->alloc_event.arena_block_handle = OS_HandleFromPtr(arena_block);                                                                \
        event->alloc_event.size = (s);                                                                                                        \
    }
#define Debug_ArenaRelease_Push(arena_block)                                                                          \
    {                                                                                                                 \
        DebugEvent* event = debug_event_record(DebugEventType::ArenaRelease, S("Arena Release"), S("Arena Release")); \
        event->handle = OS_HandleFromPtr(arena_block);                                                                \
    }
// Http debugging macros
#define Debug_Http_Push(http_result)                                                                           \
    {                                                                                                          \
        auto debug_http_result__ = (http_result);                                                              \
        DebugEvent* event = debug_event_record(DebugEventType::Http, Debug_Name("Http Push"), S("Http Push")); \
        event->http_event.async_result = (S32)debug_http_result__.result;                                      \
        event->http_event.error_code = debug_http_result__.curl_code;                                          \
    }

#define Debug_SetName(h, n)                                                                                                 \
    {                                                                                                                       \
        DebugEvent* event = debug_event_record(DebugEventType::HandleNameSet, Debug_Name("Handle Name"), S("Handle Name")); \
        event->handle_name_set_event.handle = OS_HandleFromPtr(h);                                                          \
        event->handle_name_set_event.name = (n);                                                                            \
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

#include "debug_log.cpp"

#endif
