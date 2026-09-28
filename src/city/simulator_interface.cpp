using namespace simdjson;
namespace city
{

g_internal simdjson::error_code
_simulator_coordinates_from_json(Arena* arena, ondemand::array& array, Buffer<Coordinate>* out_buffer);
g_internal void
_simulator_message_push(mi_heap_t* heap, String8List* list, String8 message);
g_internal void
_simulator_message_list_clear(String8List* list);
g_internal Buffer<String8>
_simulator_options_buffer_create(mi_heap_t* heap, String8List options);

Simulator::~Simulator()
{
    simulator_disconnect();
}

SimulationError
Simulator::simulator_connect(U32 status_update_freq_in_frames)
{
    simulator_disconnect();
    this->status_update_freq_in_frames = status_update_freq_in_frames;
    last_frame_update = 0;
    selected_scenario_idx = 0;
    message_heap = mi_heap_new();
    Assert(message_heap);
    msg_received = _simulator_options_buffer_create(message_heap, {});

    async::WebsocketConnection ws_task_result = async::async_websocket_start(S("ws://127.0.0.1:8080/ws"));
    if (ws_task_result.has_error())
    {
        return SimulationError(SimulationErrorType::Connection, ws_task_result.async_result.curl_code
                                                                    ? (U64)ws_task_result.async_result.curl_code
                                                                    : (U64)ws_task_result.async_result.result);
    }
    connection = std::move(ws_task_result);
    connected = true;
    return {};
}

void
Simulator::simulator_disconnect()
{
    connection = async::WebsocketConnection{};
    connected = false;
    msg_send_queue = {};
    msg_received = {};
    if (message_heap)
    {
        mi_heap_destroy(message_heap);
        message_heap = 0;
    }
}

void
Simulator::simulator_options_get()
{
    Assert(connected);
    ScratchScope scratch = ScratchScope(0, 0);
    String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
    String8 options_request =
        push_str8f(scratch.arena, "{\"%.*s\":%u}", str8_varg(msg_id_name), (U32)SimulationMessageKind::Options);
    _simulator_message_push(message_heap, &msg_send_queue, options_request);
}

void
Simulator::simulator_snapshot_request()
{
    if (!connected)
    {
        return;
    }
    ScratchScope scratch = ScratchScope(0, 0);
    String8 request = push_str8f(scratch.arena, "{\"msg_id\":%u}", (U32)SimulationMessageKind::RequestSnapshot);
    _simulator_message_push(message_heap, &msg_send_queue, request);
}

void
Simulator::simulator_scenario_set(U32 scenario_idx, Buffer<String8> options)
{
    Assert(connected);
    if (scenario_idx < options.size)
    {
        ScratchScope scratch = ScratchScope(0, 0);
        String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
        String8 scenario_id_name = SIMULATION_FIELD_NAME(ScenarioId);
        String8 request =
            push_str8f(scratch.arena, "{\"%.*s\":%u,\"%.*s\":%u}", str8_varg(msg_id_name),
                       (U32)SimulationMessageKind::ChangeScenario, str8_varg(scenario_id_name), scenario_idx);
        _simulator_message_push(message_heap, &msg_send_queue, request);
    }
}

void
Simulator::simulator_update(Arena* arena, CoordinateBatch* out_coords, String8List* out_options, U32 option_idx,
                            U64 cur_frame)
{
    *out_coords = {};
    *out_options = {};
    if (!connected)
    {
        return;
    }

    String8List str_list = {};
    B32 options_received = false;
    U64 server_scenario_id = this->selected_scenario_idx;
    SimulationError error =
        _simulator_interaction(arena, out_coords, &str_list, &options_received, option_idx, &server_scenario_id);
    if (error.type != SimulationErrorType::Success)
    {
        switch (error.type)
        {
            case SimulationErrorType::Json:
            {
                ERROR_LOG("Json error code: %llu\n", error.error_code);
                return;
            };

            case SimulationErrorType::Connection:
            {
                ERROR_LOG("Connection error code: %llu\n", error.error_code);
                return;
            };
            default:
            {
                ERROR_LOG("Unknown simulation layer error\n");
            };
        }
    }

    if (options_received)
    {
        mi_free(msg_received.data);
        msg_received = _simulator_options_buffer_create(message_heap, str_list);
    }

    bool first_time_after_connection = last_frame_update == 0;
    bool status_update_timeout = ((cur_frame - last_frame_update) >= status_update_freq_in_frames);
    bool option_changed = option_idx != selected_scenario_idx;
    bool server_option_mismatch = option_idx != server_scenario_id;
    if (first_time_after_connection || status_update_timeout || option_changed || server_option_mismatch)
    {
        simulator_options_get();
        last_frame_update = cur_frame;
        simulator_scenario_set(option_idx, this->msg_received);
        selected_scenario_idx = option_idx;
    }

    for (U64 idx = 0; idx < msg_received.size; ++idx)
    {
        String8 option_copy = push_str8_copy(arena, msg_received.data[idx]);
        str8_list_push(arena, out_options, option_copy);
    }
}

g_internal simdjson::error_code
_simulator_coordinates_from_json(Arena* arena, ondemand::array& array, Buffer<Coordinate>* out_buffer)
{
    prof_scope_marker;

    U64 element_count = 0;
    error_code error = array.count_elements().get(element_count);
    if (error)
    {
        return error;
    }

    Buffer<Coordinate> coord_buffer = buffer_alloc<Coordinate>(arena, element_count);
    U64 idx = 0;
    for (auto obj : array)
    {
        Coordinate* coord = coord_buffer[idx];
        CoordinateView coord_view = {};
        error = obj.get<CoordinateView>(coord_view);
        if (error)
        {
            return error;
        }

        coord->time = coord_view.time;
        coord->event_type = coord_view.event_type;
        coord->node_from_id = coord_view.node_from_id;
        coord->node_to_id = coord_view.node_to_id;
        coord->from = glm::dvec2(coord_view.lon_from, coord_view.lat_from);
        coord->to = glm::dvec2(coord_view.lon_to, coord_view.lat_to);
        String8 id_str = str8((U8*)coord_view.id.data(), coord_view.id.size());
        U64 needle_start = str8_substr_find(id_str, S("_bicycle"), 0, MatchFlag_CaseInsensitive);
        coord->vehicle_type = VehicleType::Car;
        if (needle_start < id_str.size)
        {
            coord->vehicle_type = VehicleType::Bicycle;
        }
        B32 is_integer = try_s64_from_str8_c_rules(id_str, &coord->id);
        if (!is_integer)
        {
            String8 id_prefix_str = str8(id_str.str, needle_start);
            is_integer = try_s64_from_str8_c_rules(id_prefix_str, &coord->id);
            if (!is_integer)
            {
                return simdjson::NUMBER_ERROR;
            }
        }
        idx++;
    }

    *out_buffer = coord_buffer;
    return error;
}

SimulationError
Simulator::_simulator_interaction(Arena* arena, CoordinateBatch* out_coords, String8List* out_options,
                                  B32* options_received, U32 expected_scenario_idx, U64* scenario_id)
{
    SimulationError simulation_error = {};
    ScratchScope scratch = ScratchScope(&arena, 1);
    String8List msg_list = {};
    connection.try_send_resv(scratch.arena, &msg_send_queue, &msg_list);
    _simulator_message_list_clear(&msg_send_queue);
    if (connection.has_error())
    {
        U64 error_code = connection.async_result.curl_code ? (U64)connection.async_result.curl_code
                                                           : (U64)connection.async_result.result;
        return SimulationError(SimulationErrorType::Connection, error_code);
    }
    if (msg_list.node_count)
    {
        simdjson::ondemand::parser parser;
        simdjson::error_code json_error = {};
        for (String8Node* msg_node = msg_list.first; msg_node; msg_node = msg_node->next)
        {
            String8 msg = msg_node->string;

            simdjson::ondemand::document doc;
            json_error = simulator_prepare_json_doc(scratch.arena, msg, parser, doc);
            if (json_error)
            {
                return SimulationError(SimulationErrorType::Json, json_error);
            }
            U64 msg_id = {};
            String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
            auto msg_id_field = doc[(const char*)msg_id_name.str];
            json_error = msg_id_field.get_uint64().get(msg_id);
            if (json_error)
            {
                return SimulationError(SimulationErrorType::Json, json_error);
            }

            switch ((SimulationMessageKind)msg_id)
            {
                case SimulationMessageKind::Options:
                {
                    ondemand::array list;
                    String8 options_name = SIMULATION_FIELD_NAME(Options);
                    json_error = doc[(const char*)options_name.str].get_array().get(list);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    *out_options = {};
                    *options_received = true;
                    for (auto field : list)
                    {
                        std::string_view scenario_name;
                        json_error = field.get_string().get(scenario_name);
                        if (json_error)
                        {
                            return SimulationError(SimulationErrorType::Json, json_error);
                        }
                        String8 scenario_str_name = str8((U8*)scenario_name.data(), scenario_name.size());
                        String8 scenario_copy = push_str8_copy(arena, scenario_str_name);
                        str8_list_push(arena, out_options, scenario_copy);
                    }
                };
                break;
                case SimulationMessageKind::Stream:
                case SimulationMessageKind::Reset:
                {
                    U64 id = 0;
                    ondemand::array array;
                    String8 stream_name = SIMULATION_FIELD_NAME(Stream);
                    String8 scenario_id_name = SIMULATION_FIELD_NAME(ScenarioId);
                    json_error = doc[(const char*)scenario_id_name.str].get_uint64().get(id);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    *scenario_id = id;
                    // Ignore stale coordinates, but continue processing other messages such as options.
                    if (expected_scenario_idx == 0 || id != expected_scenario_idx)
                    {
                        continue;
                    }
                    json_error = doc[(const char*)stream_name.str].get_array().get(array);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    Buffer<Coordinate> incoming = {};
                    json_error = _simulator_coordinates_from_json(arena, array, &incoming);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    // Reset currently uses the same event handling as Stream.
                    simulator_coordinate_batch_append(arena, out_coords, incoming, false);
                };
                break;
                default:
                {
                    simulation_error = SimulationError(SimulationErrorType::UnknowMsgId);
                };
            }
        }
    }

    return simulation_error;
}

g_internal void
_simulator_message_push(mi_heap_t* heap, String8List* list, String8 message)
{
    // One allocation holds both the list node and its null-terminated string.
    U64 allocation_size = sizeof(String8Node) + message.size + 1;
    String8Node* node = (String8Node*)mi_heap_zalloc(heap, allocation_size);
    Assert(node);
    U8* text = (U8*)(node + 1);
    MemoryCopy(text, message.str, message.size);
    String8 copy = str8(text, message.size);
    str8_list_push_node_set_string(list, node, copy);
}

g_internal void
_simulator_message_list_clear(String8List* list)
{
    String8Node* node = list->first;
    while (node)
    {
        String8Node* next = node->next;
        // The node and its string occupy one allocation.
        mi_free(node);
        node = next;
    }
    *list = {};
}

g_internal Buffer<String8>
_simulator_options_buffer_create(mi_heap_t* heap, String8List options)
{
    // Store the descriptors and their strings together, so one free releases the buffer.
    String8 none = S("None");
    U64 count = options.node_count + 1;
    U64 bytes = count * sizeof(String8) + none.size + 1 + options.total_size + options.node_count;
    String8* data = (String8*)mi_heap_zalloc(heap, bytes);
    Assert(data);
    U8* text = (U8*)(data + count);
    MemoryCopy(text, none.str, none.size);
    data[0] = str8(text, none.size);
    text += none.size + 1;
    U64 idx = 1;
    for (String8Node* node = options.first; node; node = node->next)
    {
        MemoryCopy(text, node->string.str, node->string.size);
        data[idx++] = str8(text, node->string.size);
        text += node->string.size + 1;
    }
    Buffer<String8> result = {};
    result.data = data;
    result.size = count;
    return result;
}

} // namespace city
