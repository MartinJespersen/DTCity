#pragma once

namespace city
{
struct ServerUpdate;
}

// Streams event-time ECEF position, unit direction and speed. Arrival positions
// may be JSON null; all other motion fields must be present in the database.
// All events require signed node IDs and way_id (the link's origid / OSM way ID).
static String8
simulator_event_window_reply(Arena* arena, sqlite3* db, const city::ServerUpdate& update, U64 request_id);

static bool
_simulator_event_append(yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array, sqlite3_stmt* event_stmt);
