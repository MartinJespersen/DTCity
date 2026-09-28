using namespace simdjson;
namespace city
{

// Stable protocol IDs, independent of symbol-set linker order.
enum class SimulationMessageKind : U32
{
    Nop = 0,
    Options = 1,
    ChangeScenario = 2,
    Stream = 3,
    RequestSnapshot = 4,
    Reset = 5,
};

enum class SimulationFieldType
{
    MsgId,
    Options,
    ScenarioId,
    Stream
};

struct SimulationFieldName
{
    SimulationFieldType type;
    String8 name;
};

// Field names are indexed by the shared enum on every platform.
static const SimulationFieldName simulation_field_names[] = {
    {SimulationFieldType::MsgId, S("msg_id")},
    {SimulationFieldType::Options, S("options")},
    {SimulationFieldType::ScenarioId, S("scenario_idx")},
    {SimulationFieldType::Stream, S("stream")},
};
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
simulator_prepare_json_doc(Arena* arena, String8 msg, ondemand::parser& parser, ondemand::document& out_doc);
} // namespace city
