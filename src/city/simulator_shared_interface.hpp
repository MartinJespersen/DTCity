#pragma once

using namespace simdjson;
namespace city
{

// Stable protocol IDs, independent of symbol-set linker order.
enum class SimulationMessageKind : U32
{
    Nop = 0,
    Stream = 3,
    MetadataRequest = 4,
    ServerUpdate = 6,
};

enum class SimulationFieldType
{
    MsgId,
    Scenarios,
    Stream,
    TimestampStart,
    TimestampEnd
};

enum class AgentEventType : S64
{
    Arrival = 4
};

struct ServerUpdate
{
    std::string name;
    F64 playback;
    F64 period;
};

struct SimulationFieldName
{
    SimulationFieldType type;
    String8 name;
};

// Field names are indexed by the shared enum on every platform.
static const SimulationFieldName simulation_field_names[] = {
    {SimulationFieldType::MsgId, S("msg_id")},
    {SimulationFieldType::Scenarios, S("scenarios")},
    {SimulationFieldType::Stream, S("stream")},
    {SimulationFieldType::TimestampStart, S("timestamp_start")},
    {SimulationFieldType::TimestampEnd, S("timestamp_end")}};
#define SIMULATION_FIELD_NAME(Type) (::city::simulation_field_names[(U32)::city::SimulationFieldType::Type].name)

enum class SimulationErrorType
{
    Success,
    Json,
    Connection,
    UnknowMsgId
};

struct SimulationError
{
    SimulationError() = default;
    SimulationError(SimulationErrorType type) : type(type), error_code(0) {};
    SimulationError(SimulationErrorType type, U64 error_code) : type(type), error_code(error_code) {};
    SimulationErrorType type;
    U64 error_code;
};

g_internal error_code
simulator_server_update_from_json(ondemand::document& doc, ServerUpdate* out_update, U64* out_request_id);

g_internal error_code
simulator_prepare_json_doc(Arena* arena, String8 msg, ondemand::parser& parser, ondemand::document& out_doc);
} // namespace city
