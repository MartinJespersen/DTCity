// Snapshots are only used to establish state after connecting or seeking.
const char simulator_event_snapshot_sql[] =
    "WITH RECURSIVE agents(agent_id) AS ("
    " SELECT min(agent_id) FROM agent_events UNION ALL "
    " SELECT (SELECT min(agent_id) FROM agent_events WHERE agent_id > agents.agent_id) "
    " FROM agents WHERE agents.agent_id IS NOT NULL ORDER BY 1) "
    "SELECT event.agent_id, event.time, event.event_type, event.node_from, event.node_to, "
    "event.node_from_lon, event.node_from_lat, event.node_to_lon, event.node_to_lat "
    "FROM agents "
    "JOIN agent_events AS event ON event.agent_info_rowid = ("
    " SELECT agent_info_rowid FROM agent_events WHERE agent_id = agents.agent_id AND time <= ?1 "
    " ORDER BY time DESC, agent_info_rowid DESC LIMIT 1) "
    "LIMIT ?2";

// Never limit event rows: doing so would lose events when the cursor advances.
const char simulator_event_delta_sql[] =
    "SELECT agent_id, time, event_type, node_from, node_to, "
    "node_from_lon, node_from_lat, node_to_lon, node_to_lat FROM agent_events "
    "WHERE time > ?1 AND time <= ?2 ORDER BY time, agent_info_rowid";

static bool
_simulator_event_needs_reset(const SimulatorEventCursor& cursor, double time,
                             unsigned long long stream_generation, unsigned int scenario)
{
    return !cursor.initialized || cursor.reset_pending || time < cursor.time ||
           stream_generation != cursor.stream_generation || scenario != cursor.scenario;
}

static bool
_simulator_event_append(yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array, sqlite3_stmt* event_stmt)
{
    const char* id = (const char*)sqlite3_column_text(event_stmt, 0);
    double time = sqlite3_column_double(event_stmt, 1);
    sqlite3_int64 event_type = sqlite3_column_int64(event_stmt, 2);
    sqlite3_int64 node_from = sqlite3_column_int64(event_stmt, 3);
    sqlite3_int64 node_to = sqlite3_column_int64(event_stmt, 4);
    double lon_from = sqlite3_column_double(event_stmt, 5);
    double lat_from = sqlite3_column_double(event_stmt, 6);
    double lon_to = sqlite3_column_double(event_stmt, 7);
    double lat_to = sqlite3_column_double(event_stmt, 8);

    // Missing coordinates cannot be represented by the receiver's numeric fields.
    for (int column = 5; column < 9; ++column)
    {
        int column_type = sqlite3_column_type(event_stmt, column);
        if (column_type == SQLITE_NULL)
        {
            return false;
        }
    }

    yyjson_mut_val* event = yyjson_mut_obj(doc);
    if (!event || !id || event_type < 0 || node_from < 0 || node_to < 0)
    {
        return false;
    }
    bool added = yyjson_mut_obj_add_strcpy(doc, event, "id", id) &&
                 yyjson_mut_obj_add_real(doc, event, "time", time) &&
                 yyjson_mut_obj_add_uint(doc, event, "event_type", event_type) &&
                 yyjson_mut_obj_add_uint(doc, event, "node_from_id", node_from) &&
                 yyjson_mut_obj_add_uint(doc, event, "node_to_id", node_to) &&
                 yyjson_mut_obj_add_real(doc, event, "lon_from", lon_from) &&
                 yyjson_mut_obj_add_real(doc, event, "lat_from", lat_from) &&
                 yyjson_mut_obj_add_real(doc, event, "lon_to", lon_to) &&
                 yyjson_mut_obj_add_real(doc, event, "lat_to", lat_to);
    if (added)
    {
        added = yyjson_mut_arr_append(snapshot_array, event);
    }
    return added;
}
