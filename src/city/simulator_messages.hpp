#pragma once

// Propagate the first parsing error from functions returning simdjson::error_code.
#define JSON_TRY(expression)                             \
    do                                                   \
    {                                                    \
        simdjson::error_code json_error_ = (expression); \
        if (json_error_)                                 \
        {                                                \
            return json_error_;                          \
        }                                                \
    } while (0)

namespace city
{

struct ServerUpdate;

enum class AgentType : S32
{
    Car,
    Bicycle,
    Count
};

struct AgentUpdate
{
    S64 id;
    String8 original_id; // Owned by the arena used to parse this update.
    AgentType vehicle_type;
    F64 time;
    S64 event_type;
    S64 node_from_id;
    S64 node_to_id;
    glm::dvec2 from;
    glm::dvec2 to;
};

struct Scenario
{
    std::string name;
    F64 timestamp_start;
    F64 timestamp_end;
};

struct SimulationMetadata
{
    std::vector<Scenario> scenarios;
};

struct CoordinateView
{
    std::string_view id;
    F64 time;
    U64 event_type;
    U64 node_from_id;
    U64 node_to_id;
    F64 lon_from;
    F64 lat_from;
    F64 lon_to;
    F64 lat_to;
};

// Metadata owns its scenario names. Output is replaced only on success.
g_internal simdjson::error_code
simulator_metadata_from_json(simdjson::ondemand::object& object, SimulationMetadata* out);

g_internal String8
simulator_server_update_to_json(Arena* arena, const ServerUpdate& update, U64 request_id = 0);

g_internal Buffer<AgentUpdate>
simulator_events_before_playback(Arena* arena, Buffer<AgentUpdate>& pending, F64 playback);

// Compact a chronological snapshot in place, keeping the last ready event per agent.
g_internal Buffer<AgentUpdate>
simulator_snapshot_latest_events(Arena* arena, Buffer<AgentUpdate> ready);

F64
simulator_playback_advance(F64 playback, F64 delta_seconds, F64 timestamp_end);

g_internal void
_simulator_message_push(std::vector<std::string>* queue, String8 message);
g_internal void
_simulator_message_push(std::vector<std::string>* queue, String8List* parts, StringJoin* join);
} // namespace city

template <>
simdjson_inline simdjson::error_code
simdjson::ondemand::value::get(city::CoordinateView& out) noexcept
{
    simdjson::ondemand::object obj;
    JSON_TRY(get_object().get(obj));
    JSON_TRY(obj["id"].get_string().get(out.id));
    JSON_TRY(obj["time"].get_double().get(out.time));
    JSON_TRY(obj["event_type"].get_uint64().get(out.event_type));
    JSON_TRY(obj["node_from_id"].get_uint64().get(out.node_from_id));
    JSON_TRY(obj["node_to_id"].get_uint64().get(out.node_to_id));
    JSON_TRY(obj["lon_from"].get_double().get(out.lon_from));
    JSON_TRY(obj["lat_from"].get_double().get(out.lat_from));
    JSON_TRY(obj["lon_to"].get_double().get(out.lon_to));
    JSON_TRY(obj["lat_to"].get_double().get(out.lat_to));

    return simdjson::SUCCESS;
}
