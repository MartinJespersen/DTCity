#include "os_core/os_core_inc.hpp"
struct AllocationEvent
{
    OS_Handle arena_handle;
    U32 cmt_size;
};

enum class DebugEventType : S32
{
    Allocation,
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
    };
};

struct DebugTable
{
    U64 arr_idx_cur;
    std::atomic<U64> arr_idx_and_event_idx; // msb 32 bit: event array index. lsb 32 bits: event idx
    // we switch between two buffers so that other threads can access the one not used by the reader
    DebugEvent events[2][16 * 65536];

    U64 total_memory_use;
};

#define Debug_Name_(file, line, func) S(file ":" line "|" func)
#define Debug_Name(name) Debug_Name_(__FILE__, __LINE__, __func__)

#if BUILD_DEBUG
DebugTable g_debug_table = {};

g_internal DebugEvent*
debug_event_record(DebugEventType event_type, String8 debug_name, String8 name)
{
    Assert((g_debug_table.arr_idx_and_event_idx.load() & 0xFFFFFFFF) < ArrayCount(g_debug_table.events[0]));
    U64 arr_idx_and_event_idx = g_debug_table.arr_idx_and_event_idx.fetch_add(1);
    DebugEvent* event = &g_debug_table.events[g_debug_table.arr_idx_cur][arr_idx_and_event_idx & 0xFFFFFFFF];
    event->debug_name = debug_name;
    event->name = name;
    event->type = event_type;
    event->tid = os_tid();
}

g_internal void
debug_event_frame_end()
{
    g_debug_table.arr_idx_cur = !g_debug_table.arr_idx_cur;
    U64 arr_idx_and_event_idx_prev = g_debug_table.arr_idx_and_event_idx.exchange(g_debug_table.arr_idx_cur << 32);
    U32 arr_idx = arr_idx_and_event_idx_prev >> 32;
    U32 event_count = arr_idx_and_event_idx_prev & 0xFFFFFFFF;

    DebugEvent* event_arr = g_debug_table.events[arr_idx];
    for (U32 idx = 0; idx < event_count; ++idx)
    {
        DebugEvent* event = &event_arr[idx];

        switch (event->type)
        {
            case DebugEventType::Allocation:
            {
                DEBUG_LOG("test of allocation");
                AllocationEvent* alloc_event = &event->alloc_event;
                // alloc_event->arena_handle
            }
            break;
            default:
            {
                ERROR_LOG("Unknown error type encountered with type %lld", event->type);
            }
        }
    }
}

#define Debug_Allocation_Push(e)                                                                                                                                                                       \
    {                                                                                                                                                                                                  \
        DebugEvent* event = debug_event_record(DebugEventType::Allocation, Debug_Name("Allocation Push"), S("Allocation Push"));                                                                       \
        event->alloc_event = e;                                                                                                                                                                        \
    }

#else
#define Debug_Allocation_Push(...)
#endif
