using namespace simdjson;
namespace city
{

g_internal error_code
simulator_server_update_from_json(ondemand::document& doc, ServerUpdate* out_update, U64* out_request_id)
{
    ServerUpdate update = {};
    std::string_view name;
    U64 request_id = 0;
    error_code error = doc["name"].get_string().get(name);
    if (error) return error;
    update.name.assign(name.data(), name.size());
    error = doc["playback"].get_double().get(update.playback);
    if (error) return error;
    error = doc["period"].get_double().get(update.period);
    if (error) return error;
    error = doc["request_id"].get_uint64().get(request_id);
    if (error) return error;
    if (update.period < 0) return simdjson::NUMBER_ERROR;
    *out_update = std::move(update);
    *out_request_id = request_id;
    return simdjson::SUCCESS;
}

g_internal simdjson::error_code
simulator_prepare_json_doc(Arena* arena, String8 msg, simdjson::ondemand::parser& parser,
                           simdjson::ondemand::document& out_doc)
{
    U64 capacity = msg.size + simdjson::SIMDJSON_PADDING;
    U8* json_data = PushArray(arena, U8, capacity);
    MemoryCopy(json_data, msg.str, msg.size);

    simdjson::padded_string_view json((const char*)json_data, msg.size, capacity);

    simdjson::error_code json_error;
    json_error = parser.iterate(json).get(out_doc);

    return json_error;
}
}; // namespace city
