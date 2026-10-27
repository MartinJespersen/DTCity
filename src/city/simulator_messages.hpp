#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>
#include "base/container.hpp"
#include "simulator_shared_interface.hpp"
#include "third_party/simdjson/simdjson.h"

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
    B32 has_node_ids; // False for arrivals on unknown links; zero is a valid node ID.
    glm::dvec3 position;
    glm::dvec3 direction;
    F64 speed;
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

struct MotionEventView
{
    std::string_view id;
    F64 time;
    U64 event_type;
    S64 node_from_id;
    S64 node_to_id;
    B32 has_node_ids;
    glm::dvec3 position;
    glm::dvec3 direction;
    F64 speed;
};

// Metadata owns its scenario names. Output is replaced only on success.
g_internal simdjson::error_code
simulator_metadata_from_json(simdjson::ondemand::object& object, SimulationMetadata* out);

g_internal String8
simulator_server_update_to_json(Arena* arena, const ServerUpdate& update, U64 request_id = 0);

// Decode motion events into arena-owned updates. Output changes only on success.
g_internal simdjson::error_code
simulator_events_from_json(Arena* arena, simdjson::ondemand::array& array, Buffer<AgentUpdate>* out_buffer);

// Consume all events at or before playback, preserving server order.
g_internal Buffer<AgentUpdate>
simulator_events_ready(Arena* arena, Buffer<AgentUpdate>& pending, F64 playback);

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
simdjson::ondemand::value::get(city::MotionEventView& out) noexcept
{
    simdjson::ondemand::object obj;
    JSON_TRY(get_object().get(obj));
    JSON_TRY(obj["id"].get_string().get(out.id));
    JSON_TRY(obj["time"].get_double().get(out.time));
    JSON_TRY(obj["event_type"].get_uint64().get(out.event_type));
    // Removal needs identity and time, even when the importer could not locate the link.
    out.position = {};
    if (out.event_type != (U64)city::AgentEventType::Arrival)
    {
        JSON_TRY(obj["position_x"].get_double().get(out.position.x));
        JSON_TRY(obj["position_y"].get_double().get(out.position.y));
        JSON_TRY(obj["position_z"].get_double().get(out.position.z));
    }
    JSON_TRY(obj["direction_x"].get_double().get(out.direction.x));
    JSON_TRY(obj["direction_y"].get_double().get(out.direction.y));
    JSON_TRY(obj["direction_z"].get_double().get(out.direction.z));
    JSON_TRY(obj["speed"].get_double().get(out.speed));

    const char* node_fields[] = {"node_from_id", "node_to_id"};
    S64* node_ids[] = {&out.node_from_id, &out.node_to_id};
    out.has_node_ids = false;
    for (U32 index = 0; index < ArrayCount(node_fields); ++index)
    {
        simdjson::ondemand::value node;
        JSON_TRY(obj[node_fields[index]].get(node));
        bool missing = false;
        JSON_TRY(node.is_null().get(missing));
        if (index == 0)
            out.has_node_ids = !missing;
        else if (out.has_node_ids != !missing)
            return simdjson::INCORRECT_TYPE;
        *node_ids[index] = 0;
        if (missing)
        {
            if (out.event_type != (U64)city::AgentEventType::Arrival) return simdjson::INCORRECT_TYPE;
        }
        else
            JSON_TRY(node.get_int64().get(*node_ids[index]));
    }

    return simdjson::SUCCESS;
}
