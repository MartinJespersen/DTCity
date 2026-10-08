#include "base/base_inc.hpp"
#include "city/simulator_shared_interface.hpp"
#include "city/simulator_messages.hpp"

namespace city
{

g_internal simdjson::error_code
simulator_metadata_from_json(simdjson::ondemand::object& object, SimulationMetadata* out)
{
    SimulationMetadata metadata = {};
    F64 timestamp_start = 0;
    F64 timestamp_end = 0;
    String8 timestamp_start_name = SIMULATION_FIELD_NAME(TimestampStart);
    String8 timestamp_end_name = SIMULATION_FIELD_NAME(TimestampEnd);
    JSON_TRY(object[(const char*)timestamp_start_name.str].get_double().get(timestamp_start));
    JSON_TRY(object[(const char*)timestamp_end_name.str].get_double().get(timestamp_end));

    simdjson::ondemand::array scenarios;
    String8 scenarios_name = SIMULATION_FIELD_NAME(Scenarios);
    JSON_TRY(object[(const char*)scenarios_name.str].get_array().get(scenarios));
    U64 count = 0;
    JSON_TRY(scenarios.count_elements().get(count));
    metadata.scenarios.reserve(count);
    for (auto scenario : scenarios)
    {
        std::string_view name;
        JSON_TRY(scenario.get_string().get(name));
        // The current protocol supplies one time range for all scenario names.
        Scenario parsed = {};
        parsed.name.assign(name.data(), name.size());
        parsed.timestamp_start = timestamp_start;
        parsed.timestamp_end = timestamp_end;
        metadata.scenarios.push_back(std::move(parsed));
    }

    *out = std::move(metadata);
    return simdjson::SUCCESS;
}

g_internal String8
simulator_server_update_to_json(Arena* arena, const ServerUpdate& update, U64 request_id)
{
    // JSON strings escape quotes, backslashes, and every control byte.
    U64 name_capacity = update.name.size() * 6;
    U8* escaped_data = PushArray(arena, U8, name_capacity + 1);
    U64 escaped_size = 0;
    const char hex_digits[] = "0123456789abcdef";
    for (U8 byte : update.name)
    {
        if (byte < 0x20)
        {
            escaped_data[escaped_size++] = '\\';
            escaped_data[escaped_size++] = 'u';
            escaped_data[escaped_size++] = '0';
            escaped_data[escaped_size++] = '0';
            escaped_data[escaped_size++] = hex_digits[byte >> 4];
            escaped_data[escaped_size++] = hex_digits[byte & 0xf];
        }
        else
        {
            if (byte == '"' || byte == '\\')
            {
                escaped_data[escaped_size++] = '\\';
            }
            escaped_data[escaped_size++] = byte;
        }
    }
    String8 escaped_name = str8(escaped_data, escaped_size);
    String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
    String8 message = push_str8f(arena, "{\"%.*s\":%u,\"name\":\"%.*s\",\"playback\":%.17g,\"period\":%.17g,\"request_id\":%llu}",
                                str8_varg(msg_id_name), (U32)SimulationMessageKind::ServerUpdate,
                                str8_varg(escaped_name), update.playback, update.period, request_id);
    return message;
}

F64
simulator_playback_advance(F64 playback, F64 delta_seconds, F64 timestamp_end)
{
    // Strictly earlier events require playback to pass the final timestamp too.
    F64 playback_end = std::nextafter(timestamp_end, HUGE_VAL);
    F64 advanced = Min(playback + delta_seconds, playback_end);
    return advanced;
}

g_internal Buffer<AgentUpdate>
simulator_events_before_playback(Arena* arena, Buffer<AgentUpdate>& pending, F64 playback)
{
    // Server events are chronological; retain the future portion for later frames.
    U64 ready_count = 0;
    while (ready_count < pending.size && pending.data[ready_count].time < playback)
    {
        ++ready_count;
    }
    Buffer<AgentUpdate> ready = buffer_alloc<AgentUpdate>(arena, ready_count);
    for (U64 index = 0; index < ready_count; ++index)
    {
        ready.data[index] = pending.data[index];
        ready.data[index].original_id = push_str8_copy(arena, pending.data[index].original_id);
    }
    if (ready_count)
    {
        pending.data += ready_count;
        pending.size -= ready_count;
    }
    return ready;
}

g_internal Buffer<AgentUpdate>
simulator_snapshot_latest_events(Arena* arena, Buffer<AgentUpdate> ready)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    Map<S64, U64>* latest_indices = Map<S64, U64>::create(scratch.arena, Max(ready.size, 1));
    for (U64 index = 0; index < ready.size; ++index)
    {
        U64* latest = {};
        MapResult result = latest_indices->get(ready.data[index].id, &latest);
        if (result == MapResult::Success)
        {
            *latest = index;
        }
        else
        {
            latest_indices->insert(scratch.arena, ready.data[index].id, index);
        }
    }

    // Preserve the order of final events, including server ordering at equal timestamps.
    U64 count = 0;
    for (U64 index = 0; index < ready.size; ++index)
    {
        U64* latest = {};
        MapResult result = latest_indices->get(ready.data[index].id, &latest);
        Assert(result == MapResult::Success);
        if (*latest == index)
        {
            ready.data[count++] = ready.data[index];
        }
    }
    ready.size = count;
    return ready;
}

g_internal void
_simulator_message_push(std::vector<std::string>* queue, String8 message)
{
    String8Node part = {};
    String8List parts = {};
    str8_list_push_node_set_string(&parts, &part, message);
    StringJoin join = {};
    _simulator_message_push(queue, &parts, &join);
}

g_internal void
_simulator_message_push(std::vector<std::string>* queue, String8List* parts, StringJoin* join)
{
    // Build the joined message once; the queue owns its string independently of the input.
    U64 separator_count = parts->node_count ? parts->node_count - 1 : 0;
    U64 message_size = join->pre.size + parts->total_size + separator_count * join->sep.size + join->post.size;
    std::string message(message_size, '\0');
    U8* cursor = (U8*)message.data();
    MemoryCopy(cursor, join->pre.str, join->pre.size);
    cursor += join->pre.size;
    for (String8Node* part = parts->first; part; part = part->next)
    {
        MemoryCopy(cursor, part->string.str, part->string.size);
        cursor += part->string.size;
        if (part->next)
        {
            MemoryCopy(cursor, join->sep.str, join->sep.size);
            cursor += join->sep.size;
        }
    }
    MemoryCopy(cursor, join->post.str, join->post.size);
    cursor += join->post.size;
    queue->push_back(std::move(message));
}

} // namespace city
