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

// Returns the registered field name as String8, from either client or simulator code.
#define SIMULATION_FIELD_NAME(Type) (SyAddress(::city::JSON_FIELD_NAME, Type)->name)

#define SYMBOL_SET_DEFINE JSON_FIELD_NAME
#define JSON_FIELD_NAME_Type SimulationFieldName
#define JSON_FIELD_NAME_elf_section ".sy.jsonfield"
#define JSON_FIELD_NAME_coff_a_section ".sy$jsonfield_a"
#define JSON_FIELD_NAME_coff_m_section ".sy$jsonfield_m"
#define JSON_FIELD_NAME_coff_z_section ".sy$jsonfield_z"
#define JSON_FIELD_NAME_marker jsonfield

extern "C"
{
#include "third_party/symbol_set/symbol_set.define.h"

#define JSON_FIELD_NAME_DEFINE(Type, Name) SyDefine(JSON_FIELD_NAME, Type) = {SimulationFieldType::Type, S(Name)}

    JSON_FIELD_NAME_DEFINE(MsgId, "msg_id");
    JSON_FIELD_NAME_DEFINE(Options, "options");
    JSON_FIELD_NAME_DEFINE(ScenarioId, "scenario_idx");
    JSON_FIELD_NAME_DEFINE(Stream, "stream");
}

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
