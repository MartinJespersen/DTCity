#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include "third_party/sqlite/sqlite3.h"
#include "third_party/yyjson/yyjson.h"
#include "city/simulator_shared_interface.hpp"
#include "scenarios.hpp"
#include "event_snapshot.hpp"

static String8
simulator_event_window_reply(Arena* arena, sqlite3* db, const city::ServerUpdate& update, U64 request_id)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    if (!doc) return {};
    defer(yyjson_mut_doc_free(doc));
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "msg_id", (U64)city::SimulationMessageKind::Stream);
    yyjson_mut_obj_add_uint(doc, root, "request_id", request_id);
    yyjson_mut_val* events = yyjson_mut_obj_add_arr(doc, root, "stream");
    if (!events) return {};

    // Include the last event before playback for each active agent, followed by the requested window.
    // A stable row ordering also preserves consecutive events with identical timestamps.
    if (!update.name.empty() && db)
    {
        const char* sql =
            "WITH RECURSIVE agents(agent_id) AS ("
            " SELECT min(agent_id) FROM agent_events UNION ALL"
            " SELECT (SELECT min(agent_id) FROM agent_events WHERE agent_id > agents.agent_id)"
            " FROM agents WHERE agents.agent_id IS NOT NULL ORDER BY 1), selected AS ("
            " SELECT event.* FROM agents JOIN agent_events AS event ON event.agent_info_rowid = ("
            " SELECT agent_info_rowid FROM agent_events WHERE agent_id = agents.agent_id AND time < ?1"
            " ORDER BY time DESC, agent_info_rowid DESC LIMIT 1) WHERE event.event_type != ?3"
            " UNION ALL SELECT * FROM agent_events WHERE time >= ?1 AND time < ?2)"
            " SELECT agent_id, time, event_type, position_x, position_y, position_z,"
            " direction_x, direction_y, direction_z, speed FROM selected ORDER BY time, agent_info_rowid";
        sqlite3_stmt* stmt = NULL;
        int result = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        defer(sqlite3_finalize(stmt));
        if (result != SQLITE_OK) return {};
        sqlite3_bind_double(stmt, 1, update.playback);
        sqlite3_bind_double(stmt, 2, update.playback + update.period);
        sqlite3_bind_int64(stmt, 3, (S64)city::AgentEventType::Arrival);
        while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            bool appended = _simulator_event_append(doc, events, stmt);
            if (!appended) return {};
        }
        if (result != SQLITE_DONE) return {};
    }
    size_t size = 0;
    char* json = yyjson_mut_write(doc, 0, &size);
    if (!json) return {};
    defer(free(json));
    String8 reply = push_str8_copy(arena, str8((U8*)json, size));
    return reply;
}

static bool
_simulator_event_append(yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array, sqlite3_stmt* event_stmt)
{
    const char* id = (const char*)sqlite3_column_text(event_stmt, 0);
    double time = sqlite3_column_double(event_stmt, 1);
    sqlite3_int64 event_type = sqlite3_column_int64(event_stmt, 2);

    yyjson_mut_val* event = yyjson_mut_obj(doc);
    if (!event || !id || event_type < 0)
    {
        return false;
    }
    bool added = yyjson_mut_obj_add_strcpy(doc, event, "id", id) &&
                 yyjson_mut_obj_add_real(doc, event, "time", time) &&
                 yyjson_mut_obj_add_uint(doc, event, "event_type", event_type);

    // Motion values are ECEF metres, a unit direction, and metres per second.
    // Only arrival positions may be NULL: removal does not require coordinates.
    const char* motion_fields[] = {"position_x", "position_y", "position_z",
                                   "direction_x", "direction_y", "direction_z", "speed"};
    for (U32 index = 0; added && index < ArrayCount(motion_fields); ++index)
    {
        int column = 3 + (int)index;
        bool missing = sqlite3_column_type(event_stmt, column) == SQLITE_NULL;
        if (missing)
        {
            if (index >= 3 || event_type != (S64)city::AgentEventType::Arrival) return false;
            added = yyjson_mut_obj_add_null(doc, event, motion_fields[index]);
        }
        else
        {
            added = yyjson_mut_obj_add_real(doc, event, motion_fields[index], sqlite3_column_double(event_stmt, column));
        }
    }
    if (added)
    {
        added = yyjson_mut_arr_append(snapshot_array, event);
    }
    return added;
}
