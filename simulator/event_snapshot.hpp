#pragma once

extern const char simulator_event_snapshot_sql[];
extern const char simulator_event_delta_sql[];

struct SimulatorEventCursor
{
    bool initialized;
    bool reset_pending;
    double time;
    unsigned long long stream_generation;
    unsigned int scenario;
};

static bool
_simulator_event_needs_reset(const SimulatorEventCursor& cursor, double time,
                             unsigned long long stream_generation, unsigned int scenario);

static bool
_simulator_event_append(yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array, sqlite3_stmt* event_stmt);
