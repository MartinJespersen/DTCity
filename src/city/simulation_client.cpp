using namespace simdjson;
namespace city
{

g_internal simdjson::error_code
_simulator_coordinates_from_json(Arena* arena, ondemand::array& array, Buffer<AgentUpdate>* out_buffer);

SimulationClient::~SimulationClient()
{
    disconnect();
}

SimulationError
SimulationClient::connect()
{
    disconnect();

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
SimulationClient::disconnect()
{
    connection = async::WebsocketConnection{};
    connected = false;
    msg_send_queue.clear();
}

void
SimulationClient::server_update_send(const ServerUpdate& update, U64 request_id)
{
    ScratchScope scratch = ScratchScope(0, 0);
    String8 message = simulator_server_update_to_json(scratch.arena, update, request_id);
    expected_request_id = request_id;
    _simulator_message_push(&msg_send_queue, message);
}

void
SimulationClient::metadata_request()
{
    if (!this->connected)
    {
        return;
    }
    ScratchScope scratch = ScratchScope(0, 0);
    String8 request = push_str8f(scratch.arena, "{\"msg_id\":%u}", (U32)SimulationMessageKind::MetadataRequest);
    _simulator_message_push(&this->msg_send_queue, request);
}

SimulationClient::StreamUpdate
SimulationClient::update(Arena* arena)
{
    StreamUpdate update = {};
    if (!connected)
    {
        return {};
    }

    SimulationError error = _update(arena, &update);
    if (error.type != SimulationErrorType::Success)
    {
        switch (error.type)
        {
            case SimulationErrorType::Json:
            {
                ERROR_LOG("Json error code: %llu\n", error.error_code);
                return {};
            };

            case SimulationErrorType::Connection:
            {
                ERROR_LOG("Connection error code: %llu\n", error.error_code);
                return {};
            };
            default:
            {
                ERROR_LOG("Unknown simulation layer error\n");
            };
        }
    }

    // Return arena-owned metadata every frame, even when no new reply arrives.
    return update;
}

g_internal simdjson::error_code
_simulator_coordinates_from_json(Arena* arena, ondemand::array& array, Buffer<AgentUpdate>* out_buffer)
{
    prof_scope_marker;

    U64 element_count = 0;
    error_code error = array.count_elements().get(element_count);
    if (error)
    {
        return error;
    }

    Buffer<AgentUpdate> coord_buffer = buffer_alloc<AgentUpdate>(arena, element_count);
    U64 idx = 0;
    for (auto obj : array)
    {
        AgentUpdate* coord = &coord_buffer[idx];
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
        coord->original_id = push_str8_copy(arena, id_str);
        U64 needle_start = str8_substr_find(id_str, S("_bicycle"), 0, MatchFlag_CaseInsensitive);
        coord->vehicle_type = AgentType::Car;
        if (needle_start < id_str.size)
        {
            coord->vehicle_type = AgentType::Bicycle;
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
SimulationClient::_update(Arena* arena, StreamUpdate* out_stream_update)
{
    SimulationError simulation_error = {};
    ScratchScope scratch = ScratchScope(&arena, 1);
    String8List msg_list = {};
    String8List send_list = {};
    for (std::string& message : msg_send_queue)
    {
        String8 view = str8((U8*)message.data(), message.size());
        str8_list_push(scratch.arena, &send_list, view);
    }
    // The websocket copies the payloads before returning.
    connection.try_send_resv(scratch.arena, &send_list, &msg_list);
    msg_send_queue.clear();
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
        Buffer<AgentUpdate> agent_update_buffer = {};
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
            ondemand::object object;
            json_error = doc.get_object().get(object);
            if (json_error)
            {
                return SimulationError(SimulationErrorType::Json, json_error);
            }
            String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
            auto msg_id_field = object[(const char*)msg_id_name.str];
            json_error = msg_id_field.get_uint64().get(msg_id);
            if (json_error)
            {
                return SimulationError(SimulationErrorType::Json, json_error);
            }

            switch ((SimulationMessageKind)msg_id)
            {
                case SimulationMessageKind::MetadataRequest:
                {
                    SimulationMetadata received_metadata = {};
                    json_error = simulator_metadata_from_json(object, &received_metadata);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    out_stream_update->update_type |= UpdateType::Metadata;
                    out_stream_update->metadata = std::move(received_metadata);
                };
                break;
                case SimulationMessageKind::Stream:
                {
                    U64 request_id = 0;
                    json_error = object["request_id"].get_uint64().get(request_id);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    if (request_id != expected_request_id)
                    {
                        break;
                    }
                    ondemand::array array;
                    String8 stream_name = SIMULATION_FIELD_NAME(Stream);
                    out_stream_update->update_type |= UpdateType::Stream;
                    json_error = object[(const char*)stream_name.str].get_array().get(array);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    Buffer<AgentUpdate> incoming = {};
                    json_error = _simulator_coordinates_from_json(arena, array, &incoming);
                    if (json_error)
                    {
                        return SimulationError(SimulationErrorType::Json, json_error);
                    }
                    agent_update_buffer = incoming;
                };
                break;
                default:
                {
                    simulation_error = SimulationError(SimulationErrorType::UnknowMsgId);
                };
            }
        }
        out_stream_update->batch = agent_update_buffer;
    }

    return simulation_error;
}

} // namespace city
