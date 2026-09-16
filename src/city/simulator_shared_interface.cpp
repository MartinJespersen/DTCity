using namespace simdjson;
namespace city
{

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
