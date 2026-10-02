static String8
simulator_metadata_reply(Arena* arena, String8* scenario_arr, U32 num_scenarios,
                         F64 timestamp_start, F64 timestamp_end)
{
    String8 reply_str = {};
    yyjson_mut_doc* reply_doc = yyjson_mut_doc_new(NULL);
    if (!reply_doc)
    {
        return {};
    }
    defer(yyjson_mut_doc_free(reply_doc));

    yyjson_mut_val* reply_root = yyjson_mut_obj(reply_doc);
    if (!reply_root)
    {
        return {};
    }
    yyjson_mut_doc_set_root(reply_doc, reply_root);
    String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
    bool id_added = yyjson_mut_obj_add_uint(reply_doc, reply_root, (const char*)msg_id_name.str,
                                            (U64)city::SimulationMessageKind::MetadataRequest);
    if (!id_added)
    {
        return {};
    }
    String8 scenarios_name = SIMULATION_FIELD_NAME(Scenarios);
    yyjson_mut_val* reply_list = yyjson_mut_obj_add_arr(reply_doc, reply_root, (const char*)scenarios_name.str);
    if (!reply_list)
    {
        return {};
    }
    for (U32 i = 0; i < num_scenarios; i++)
    {
        bool item_added = yyjson_mut_arr_add_str(reply_doc, reply_list, (char*)scenario_arr[i].str);
        if (!item_added)
        {
            return {};
        }
    }

    String8 start_name = SIMULATION_FIELD_NAME(TimestampStart);
    String8 end_name = SIMULATION_FIELD_NAME(TimestampEnd);
    bool start_added = yyjson_mut_obj_add_real(reply_doc, reply_root, (const char*)start_name.str, timestamp_start);
    bool end_added = yyjson_mut_obj_add_real(reply_doc, reply_root, (const char*)end_name.str, timestamp_end);
    if (!start_added || !end_added)
    {
        return {};
    }

    size_t reply_size = 0;
    char* reply = yyjson_mut_write(reply_doc, 0, &reply_size);
    if (!reply)
    {
        return {};
    }
    defer(free(reply));

    reply_str = push_str8_copy(arena, str8((U8*)reply, reply_size));

    return reply_str;
}

