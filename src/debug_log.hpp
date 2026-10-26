#pragma once

#include <atomic>
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_strings.hpp"
#include "base/container.hpp"

#include "os_core/os_core_inc.hpp"
#include "os_core/os_core.hpp"

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

// HTTP debugging /////////////////////
struct DebugHttpEvent
{
    S32 async_result;
    U32 error_code;
};
///////////////////////////////////////
enum class DebugEventType : S32
{
    Asset = 7,
    Http = 8

};

struct DebugArenaMetrics
{
    // Used includes block headers and alignment, not unused gaps between chained blocks.
    U64 used;
    U64 committed;
    U64 reserved;
    U64 block_count;
};

struct DebugArenaSnapshot
{
    U64 arena_id;
    U32 tid;
    U32 source_line;
    const char* source_file;
    char name[96];
    DebugArenaMetrics metrics;
};

struct DebugArenaUpdate
{
    DebugArenaSnapshot snapshot;
    B32 released;
};

// Records outlive the virtual memory they describe. Only the frame thread frees them.
struct DebugArena
{
    DebugArena* next;
    DebugArena* prev;
    U64 arena_id;
    // The arena owner, pending handoff, and frame thread each own a different slot.
    // As with Arena itself, callers must serialize writers when ownership changes threads.
    DebugArenaUpdate updates[3];
    U32 write_index;
    U32 read_index;
    std::atomic<U32> pending_index; // Low two bits: slot index. Bit 2: a completed update is available.
};

struct DebugEvent
{
    U32 tid;

    DebugEventType type;
    union
    {
        AssetEvent asset_event;
        DebugHttpEvent http_event;
    };
};

struct DebugTable
{
    Arena* frame_arena;
    U64 arr_idx_cur;
    std::atomic<U64> arr_idx_and_event_idx; // msb 32 bit: event array index. lsb 32 bits: event idx
    // we switch between two buffers so that other threads can access the one not used by the reader
    DebugEvent events[2][16 * 65536];

    ChunkList<DebugEvent> asset_events;
    ChunkList<DebugEvent> http_events;

    // memory debugging
    DebugArena* debug_arena_first;
    DebugArena* debug_arena_last;
    std::atomic<DebugArena*> debug_arena_pending;
    Buffer<DebugArenaSnapshot> arena_snapshots; // Completed frame; storage belongs to frame_arena.
};

#define Debug_Name_(file, line, label) S(file ":" Stringify(line) "|" label)
#define Debug_Name(label) Debug_Name_(__FILE__, __LINE__, label)

extern DebugTable g_debug_table;

DebugEvent*
debug_event_record(DebugEventType event_type, String8 debug_name, String8 name);

void
debug_dir_create(String8 debug_dir);

void
debug_g_state_init();

void
debug_dump_str(String8 str, String8 filepath);

void
debug_event_frame_end();

void
debug_memory_snapshot_dump();

void
debug_arena_create(Arena* arena);

void
debug_arena_update(Arena* arena);

void
debug_arena_release(Arena* arena);

void
debug_arena_set_name(Arena* arena, const char* name);

void
debug_arena_set_owner(Arena* arena, Arena* block);

void
debug_arena_set_source(Arena* arena, const char* source_file, U32 source_line);

// Frame thread only. Copy the last completed frame into the supplied arena.
Buffer<DebugArenaSnapshot>
debug_arena_snapshot(Arena* arena);

void
debug_arena_window();

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
#define Debug_ArenaWindow() debug_arena_window()

// Arena debugging macros
#define Debug_ArenaCreate_Push(arena_block, s) debug_arena_create(arena_block)
#define Debug_PageAllocation_Push(arena_block, s) debug_arena_update(arena_block)
#define Debug_PageRelease_Push(arena_block, s) debug_arena_update(arena_block)
#define Debug_ArenaRelease_Push(arena_block) debug_arena_release(arena_block)
#define Debug_ArenaUsage_Push(arena_block) debug_arena_update(arena_block)
#define Debug_ArenaChain_Push(arena, arena_block) debug_arena_set_owner(arena, arena_block)
#define Debug_ArenaSource_Push(arena, source_file, source_line) debug_arena_set_source(arena, source_file, source_line)
// Http debugging macros
#define Debug_Http_Push(http_result)                                                                           \
    {                                                                                                          \
        auto debug_http_result__ = (http_result);                                                              \
        DebugEvent* event = debug_event_record(DebugEventType::Http, Debug_Name("Http Push"), S("Http Push")); \
        event->http_event.async_result = (S32)debug_http_result__.result;                                      \
        event->http_event.error_code = debug_http_result__.curl_code;                                          \
    }

#define Debug_SetName(h, n) debug_arena_set_name(h, n)
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

g_internal int
_debug_arena_compare_id(const void* lhs, const void* rhs);

#if !BUILD_TEST
g_internal int
_debug_arena_compare_committed(const void* lhs, const void* rhs);
#endif

g_internal void
_debug_arena_collect();

g_internal void
_debug_arena_publish(DebugArena* record);

g_internal Buffer<DebugArenaSnapshot>
_debug_arena_aggregate(Arena* arena);

#endif
