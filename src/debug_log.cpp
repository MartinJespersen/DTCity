#if BUILD_TEST
#include "base/base_inc.hpp"
#else
#include "core_inc.hpp"
#endif
#include "debug_log.hpp"
#include "base/base_arena.hpp"
#include "base/base_core.hpp"
#include "base/base_strings.hpp"
#include "base/container_templates.hpp"
#include "diagnostics.hpp"
#include "os_core/os_core.hpp"

#if BUILD_DEBUG
DebugTable g_debug_table = {};

DebugEvent*
debug_event_record(DebugEventType event_type, String8, String8)
{
    // Keep the name parameters for existing callers; logs use only the event payload.
    Assert((g_debug_table.arr_idx_and_event_idx.load() & 0xFFFFFFFF) < ArrayCount(g_debug_table.events[0]));
    U64 arr_idx_and_event_idx = g_debug_table.arr_idx_and_event_idx.fetch_add(1);
    U64 arr_idx_cur = arr_idx_and_event_idx >> 32;
    U64 event_idx = arr_idx_and_event_idx & 0xFFFFFFFF;
    DebugEvent* event = &g_debug_table.events[arr_idx_cur][event_idx];
    event->type = event_type;
    event->tid = os_tid();
    return event;
}

void
debug_dir_create(String8 debug_dir)
{
    if (!os_file_path_exists(debug_dir) && !os_make_directory(debug_dir))
    {
        ERROR_LOG("Could not create debug directory: %.*s\n", str8_varg(debug_dir));
        return;
    }
}

void
debug_g_state_init()
{
    g_debug_table.frame_arena = arena_alloc();
    Debug_SetName(g_debug_table.frame_arena, "debug frame arena");
    // setup debug path
    String8 debug_dir = S("debug");
    os_clear_directory(debug_dir);
    debug_dir_create(debug_dir);
}

void
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

void
debug_arena_create(Arena* arena)
{
    // Do not allocate tracking records from the arenas that they track.
    DebugArena* record = new (mi_new(sizeof(DebugArena))) DebugArena{};
    record->arena_id = IntFromPtr(arena);
    record->pending_index.store(1, std::memory_order_relaxed);
    record->read_index = 2;
    DebugArenaSnapshot* snapshot = &record->updates[record->write_index].snapshot;
    snapshot->arena_id = record->arena_id;
    snapshot->tid = os_tid();
    snapshot->metrics.reserved = arena->res;
    snapshot->metrics.block_count = 1;
    MemoryCopy(snapshot->name, "<unnamed>", sizeof("<unnamed>"));
    arena->debug_record = record;
    debug_arena_update(arena);

    // Multi-producer publication. The frame thread alone owns the active list.
    DebugArena* pending = g_debug_table.debug_arena_pending.load(std::memory_order_relaxed);
    do
    {
        record->next = pending;
    } while (!g_debug_table.debug_arena_pending.compare_exchange_weak(
        pending, record, std::memory_order_release, std::memory_order_relaxed));
}

void
debug_arena_update(Arena* arena)
{
    DebugArena* record = arena->debug_record;
    U64 destructor_used = arena->destructor_pos ? arena->res - arena->destructor_pos : 0;
    U64 destructor_committed = arena->res - arena->debug_destructor_cmt_pos;
    U64 committed = arena->cmt + Min(arena->res - arena->cmt, destructor_committed);

    DebugArenaSnapshot* snapshot = &record->updates[record->write_index].snapshot;
    snapshot->metrics.used = arena->pos + destructor_used;
    snapshot->metrics.committed = committed;
    _debug_arena_publish(record);
}

void
debug_arena_release(Arena* arena)
{
    // No arena memory is read by the UI, including after this virtual mapping is released.
    DebugArena* record = arena->debug_record;
    record->updates[record->write_index].released = true;
    _debug_arena_publish(record);
}

void
debug_arena_set_name(Arena* arena, const char* name)
{
    // Copy names so temporary task-name strings can end with their owning arena.
    DebugArena* record = arena->debug_record;
    DebugArenaSnapshot* snapshot = &record->updates[record->write_index].snapshot;
    U64 index = 0;
    for (; index + 1 < ArrayCount(snapshot->name) && name[index]; ++index)
    {
        snapshot->name[index] = name[index];
    }
    snapshot->name[index] = 0;
    _debug_arena_publish(record);
}

void
debug_arena_set_owner(Arena* arena, Arena* block)
{
    DebugArena* record = block->debug_record;
    record->updates[record->write_index].snapshot.arena_id = IntFromPtr(arena);
    _debug_arena_publish(record);
}

void
debug_arena_set_source(Arena* arena, const char* source_file, U32 source_line)
{
    DebugArena* record = arena->debug_record;
    DebugArenaSnapshot* snapshot = &record->updates[record->write_index].snapshot;
    snapshot->source_line = source_line;
    snapshot->source_file = source_file; // Compiler file literals have static lifetime.
    _debug_arena_publish(record);
}

Buffer<DebugArenaSnapshot>
debug_arena_snapshot(Arena* arena)
{
    Buffer<DebugArenaSnapshot> completed = g_debug_table.arena_snapshots;
    Buffer<DebugArenaSnapshot> result = buffer_alloc<DebugArenaSnapshot>(arena, completed.size);
    if (completed.size) MemoryCopy(result.data, completed.data, completed.size * sizeof(DebugArenaSnapshot));
    return result;
}

void
debug_event_frame_end()
{
    g_debug_table.arr_idx_cur = !g_debug_table.arr_idx_cur;
    U64 arr_idx_and_event_idx_prev = g_debug_table.arr_idx_and_event_idx.exchange(g_debug_table.arr_idx_cur << 32);
    U32 arr_idx = arr_idx_and_event_idx_prev >> 32;
    U32 event_count = arr_idx_and_event_idx_prev & 0xFFFFFFFF;

    // Initialize and reset storage for the next completed frame.
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

    // Consume only completed updates. Workers continue without a frame barrier.
    _debug_arena_collect();
    g_debug_table.arena_snapshots = _debug_arena_aggregate(g_debug_table.frame_arena);
}

void
debug_memory_snapshot_dump()
{
    ScratchScope scratch = ScratchScope(&g_debug_table.frame_arena, 1);
    Buffer<DebugArenaSnapshot> arenas = debug_arena_snapshot(scratch.arena);
    DebugArenaMetrics total = {};
    for (DebugArenaSnapshot& arena : arenas)
    {
        total.used += arena.metrics.used;
        total.committed += arena.metrics.committed;
        total.reserved += arena.metrics.reserved;
        DEBUG_LOG("Arena: %s, used: %llu, committed: %llu, reserved: %llu, blocks: %llu, created at: %s:%u", arena.name,
                  arena.metrics.used, arena.metrics.committed, arena.metrics.reserved, arena.metrics.block_count,
                  arena.source_file ? arena.source_file : "<unknown>", arena.source_line);
    }
    DEBUG_LOG("Arenas: %llu, used: %llu, committed: %llu, reserved: %llu", arenas.size, total.used, total.committed,
              total.reserved);
}

#if !BUILD_TEST
void
debug_arena_window()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(Min(1100.0f, viewport->WorkSize.x - 32.0f), Min(560.0f, viewport->WorkSize.y - 32.0f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.95f);
    if (ImGui::Begin("Arena Memory"))
    {
        ScratchScope scratch = ScratchScope(&g_debug_table.frame_arena, 1);
        Buffer<DebugArenaSnapshot> arenas = debug_arena_snapshot(scratch.arena);
        qsort(arenas.data, arenas.size, sizeof(DebugArenaSnapshot), _debug_arena_compare_committed);

        DebugArenaMetrics total = {};
        for (DebugArenaSnapshot& arena : arenas)
        {
            total.used += arena.metrics.used;
            total.committed += arena.metrics.committed;
            total.reserved += arena.metrics.reserved;
            total.block_count += arena.metrics.block_count;
        }
        String8 used = str8_from_memory_size(scratch.arena, total.used);
        String8 committed = str8_from_memory_size(scratch.arena, total.committed);
        String8 reserved = str8_from_memory_size(scratch.arena, total.reserved);
        String8 unused = str8_from_memory_size(scratch.arena, total.committed - total.used);
        ImGui::Text("%llu arenas / %llu blocks", arenas.size, total.block_count);
        ImGui::Text("Used: %.*s    Committed: %.*s    Reserved: %.*s", str8_varg(used), str8_varg(committed),
                    str8_varg(reserved));
        ImGui::Text("Committed but unused: %.*s", str8_varg(unused));
        ImGui::TextWrapped("Used includes headers, alignment, and destructor records. Chain blocks and cached blocks are grouped with their arena.");
        ImGui::TextWrapped("Reserved is virtual address space. Committed is accessible memory, not a measure of resident RAM. Heap and GPU allocations are excluded.");

        local_persist ImGuiTextFilter filter;
        filter.Draw("Filter name or source", 280.0f);
        U64 visible_count = 0;
        for (DebugArenaSnapshot& arena : arenas)
        {
            String8 filter_text = {};
            if (filter.IsActive())
                filter_text = push_str8f(scratch.arena, "%s %s", arena.name, arena.source_file ? arena.source_file : "");
            if (!filter.IsActive() || filter.PassFilter((const char*)filter_text.str, (const char*)filter_text.str + filter_text.size))
                arenas[visible_count++] = arena;
        }

        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        F32 inner_width = 0;
        if (ImGui::GetContentRegionAvail().x < 720.0f)
        {
            flags |= ImGuiTableFlags_ScrollX;
            inner_width = 720.0f;
        }
        if (ImGui::BeginTable("Arenas", 6, flags, ImVec2(0.0f, 0.0f), inner_width))
        {
            ImGui::TableSetupScrollFreeze(inner_width ? 1 : 0, 1);
            ImGui::TableSetupColumn("Arena / created at", ImGuiTableColumnFlags_None, 2.8f);
            ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_None, 1.2f);
            ImGui::TableSetupColumn("Committed", ImGuiTableColumnFlags_None, 1.2f);
            ImGui::TableSetupColumn("Reserved", ImGuiTableColumnFlags_None, 1.2f);
            ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_None, 0.65f);
            ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_None, 1.5f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)visible_count);
            while (clipper.Step())
            {
                for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index)
                {
                    DebugArenaSnapshot& arena = arenas[row_index];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(arena.name[0] ? arena.name : "<unnamed>");
                    if (arena.source_file)
                    {
                        String8 filename = str8_skip_last_slash(str8_c_string(arena.source_file));
                        ImGui::TextDisabled("%.*s:%u", str8_varg(filename), arena.source_line);
                    }
                    else
                        ImGui::TextDisabled("Location unavailable");
                    ImGui::EndGroup();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s\nCreated at: %s:%u\nAddress: 0x%llx\nCreated on thread %u", arena.name,
                                          arena.source_file ? arena.source_file : "<unknown>", arena.source_line,
                                          arena.arena_id, arena.tid);
                    U64 sizes[] = {arena.metrics.used, arena.metrics.committed, arena.metrics.reserved};
                    for (U64 size : sizes)
                    {
                        ImGui::TableNextColumn();
                        String8 text = str8_from_memory_size(scratch.arena, size);
                        ImGui::Text("%.*s", str8_varg(text));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%llu bytes", size);
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", arena.metrics.block_count);
                    ImGui::TableNextColumn();
                    F32 fraction = arena.metrics.committed ? (F32)((F64)arena.metrics.used / arena.metrics.committed) : 0;
                    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Used / committed");
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
#endif

g_internal int
_debug_arena_compare_id(const void* lhs, const void* rhs)
{
    U64 a = ((const DebugArenaSnapshot*)lhs)->arena_id;
    U64 b = ((const DebugArenaSnapshot*)rhs)->arena_id;
    return (a > b) - (a < b);
}

#if !BUILD_TEST
g_internal int
_debug_arena_compare_committed(const void* lhs, const void* rhs)
{
    U64 a = ((const DebugArenaSnapshot*)lhs)->metrics.committed;
    U64 b = ((const DebugArenaSnapshot*)rhs)->metrics.committed;
    return a == b ? _debug_arena_compare_id(lhs, rhs) : (a < b ? 1 : -1);
}
#endif

g_internal void
_debug_arena_collect()
{
    DebugArena* pending = g_debug_table.debug_arena_pending.exchange(0, std::memory_order_acquire);
    for (DebugArena* next = 0; pending; pending = next)
    {
        next = pending->next;
        DLLPushBack(g_debug_table.debug_arena_first, g_debug_table.debug_arena_last, pending);
    }
    for (DebugArena *record = g_debug_table.debug_arena_first, *next = 0; record; record = next)
    {
        next = record->next;
        if (record->pending_index.load(std::memory_order_relaxed) & 4)
        {
            // Return the previous read slot before taking the latest completed one.
            // A producer never writes the slot currently owned by this thread.
            record->read_index = record->pending_index.exchange(record->read_index, std::memory_order_acq_rel) & 3;
        }
        if (record->updates[record->read_index].released)
        {
            DLLRemove(g_debug_table.debug_arena_first, g_debug_table.debug_arena_last, record);
            record->~DebugArena();
            mi_free(record);
        }
    }
}

g_internal void
_debug_arena_publish(DebugArena* record)
{
    U32 write_index = record->write_index;
    if (record->updates[write_index].released)
    {
        // This must be the writer's last access: the frame thread can now free the record.
        record->pending_index.store(write_index | 4, std::memory_order_release);
    }
    else
    {
        U32 recycled_index = record->pending_index.exchange(write_index | 4, std::memory_order_acq_rel) & 3;
        // The reader only reads the published slot. Keep the next writer slot up to date.
        // Replacing a pending update coalesces changes without allocating more storage.
        record->updates[recycled_index] = record->updates[write_index];
        record->write_index = recycled_index;
    }
}

g_internal Buffer<DebugArenaSnapshot>
_debug_arena_aggregate(Arena* arena)
{
    U64 block_count = 0;
    for (DebugArena* record = g_debug_table.debug_arena_first; record; record = record->next) ++block_count;

    Buffer<DebugArenaSnapshot> result = buffer_alloc<DebugArenaSnapshot>(arena, block_count);
    U64 index = 0;
    for (DebugArena* record = g_debug_table.debug_arena_first; record; record = record->next)
    {
        const DebugArenaSnapshot& snapshot = record->updates[record->read_index].snapshot;
        DebugArenaSnapshot* row = &result[index++];
        row->arena_id = snapshot.arena_id;
        row->metrics = snapshot.metrics;
        if (row->arena_id == record->arena_id) *row = snapshot;
    }

    // Sum active and cached chain blocks without counting unused gaps in base_pos.
    qsort(result.data, result.size, sizeof(DebugArenaSnapshot), _debug_arena_compare_id);
    U64 arena_count = 0;
    for (U64 block_index = 0; block_index < result.size; ++block_index)
    {
        DebugArenaSnapshot row = result[block_index];
        if (arena_count && result[arena_count - 1].arena_id == row.arena_id)
        {
            DebugArenaSnapshot* owner = &result[arena_count - 1];
            owner->metrics.used += row.metrics.used;
            owner->metrics.committed += row.metrics.committed;
            owner->metrics.reserved += row.metrics.reserved;
            owner->metrics.block_count++;
            // Only the root block carries creation metadata, including an empty name.
            if (row.tid)
            {
                MemoryCopyArray(owner->name, row.name);
                owner->tid = row.tid;
                owner->source_file = row.source_file;
                owner->source_line = row.source_line;
            }
        }
        else
            result[arena_count++] = row;
    }
    result.size = arena_count;
    return result;
}

#endif
