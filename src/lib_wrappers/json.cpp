namespace wrapper
{

static osm::RoadNodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size)
{
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    simdjson::padded_string json_padded((char*)json.str, json.size);
    simdjson::error_code error = parser.iterate(json_padded).get(doc);
    U32 error_num = 0;
    bool error_ret = false;

    Buffer<osm::RoadNodeList> nodes = {};
    if (error)
    {
        goto early_ret;
    }

    nodes = BufferAlloc<osm::RoadNodeList>(arena, node_hashmap_size);
    {
        simdjson::ondemand::array elements;
        error = doc["elements"].get(elements);
        if (error)
        {
            goto early_ret;
        }

        for (auto item : elements)
        {
            auto item_object = item.get_object();
            std::string_view node_key;
            error = item_object.find_field("type").get(node_key);
            if (error)
            {
                goto early_ret;
            }

            if (node_key == "node")
            {
                osm::RoadNode* node = PushStruct(arena, osm::RoadNode);
                U64 id;
                F64 lat, lon;
                auto item_value = item.value();
                error_num |= item_value["id"].get(id);
                error_num |= item_value["lat"].get_double().get(lat);
                error_num |= item_value["lon"].get_double().get(lon);
                if (error_num)
                {
                    goto early_ret;
                }
                node->id = id;
                node->lat = lat;
                node->lon = lon;

                U64 node_slot = node->id % node_hashmap_size;
                SLLQueuePush(nodes.data[node_slot].first, nodes.data[node_slot].last, node);
            }
        }
    }
early_ret:
    if (error || error_num)
    {
        DEBUG_LOG("Error in node buffer parsing\n");
        error_ret = true;
    }
    osm::RoadNodeParseResult res = {nodes, error_ret};
    return res;
}

static osm::WayParseResult
way_buffer_from_simd_json(Arena* arena, String8 json)
{
    simdjson::dom::parser parser;
    simdjson::dom::array elements;
    simdjson::padded_string json_padded((char*)json.str, json.size);
    auto error = parser.parse(json_padded).get_object()["elements"].get(elements);
    U32 error_num = 0;
    bool error_ret = false;

    Buffer<osm::Way> way_buffer = {};
    U64 way_index = 0;
    U64 way_count = 0;

    if (error)
    {
        goto early_ret;
    }

    for (auto item : elements)
    {
        std::string_view node_key;
        error = item.get_object().at_key("type").get(node_key);
        if (error)
        {
            goto early_ret;
        }
        if (node_key == "way")
        {
            way_count += 1;
        }
    }

    way_buffer = BufferAlloc<osm::Way>(arena, way_count);
    for (auto elem : elements)
    {
        std::string_view elem_key;
        error = elem.get_object()["type"].get(elem_key);
        if (error)
        {
            goto early_ret;
        }
        if (elem_key == "way")
        {
            // ~mgj: Insert into hashmap
            U64 way_id = elem["id"].get_uint64();
            osm::Way* way = &way_buffer.data[way_index];

            way->id = way_id;
            // Get the nodes array and count elements
            auto nodes_array = elem["nodes"].get_array();
            way->node_count = nodes_array.size();

            way->node_ids = PushArray(arena, U64, way->node_count);
            U32 node_index = 0;
            for (auto node_id : nodes_array)
            {
                way->node_ids[node_index] = node_id.get_uint64();
                node_index++;
            }

            // Count tags by iterating through the object
            auto tags_object = elem["tags"].get_object();
            U64 tag_count = 0;
            for (auto _ : tags_object)
            {
                (void)_;
                tag_count++;
            }

            // Reset and iterate again to store the tags
            tags_object = elem["tags"].get_object();
            way->tags = BufferAlloc<osm::Tag>(arena, tag_count);
            U64 tag_cur_index = 0;
            for (auto tag : tags_object)
            {
                // Get key and value as string_view
                auto key_view = tag.value.get_string();
                auto value_view = tag.value.get_string();

                // Convert to String8
                String8 temp_key = Str8((U8*)key_view.value().data(), key_view.value().size());
                String8 temp_value =
                    Str8((U8*)value_view.value().data(), value_view.value().size());

                // Copy to arena
                way->tags.data[tag_cur_index].key = PushStr8Copy(arena, temp_key);
                way->tags.data[tag_cur_index].value = PushStr8Copy(arena, temp_value);

                tag_cur_index++;
            }

            way_index++;
        }
    }
early_ret:
    if (error || error_num)
    {
        DEBUG_LOG("Error in way buffer parsing\n");
        error_ret = true;
    }

    osm::WayParseResult res = {way_buffer, error_ret};
    return res;
}

} // namespace wrapper
