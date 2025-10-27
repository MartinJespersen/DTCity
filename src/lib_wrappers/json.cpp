namespace wrapper
{

static Buffer<osm_RoadNodeList>
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string json_padded((char*)json.str, json.size);
    simdjson::ondemand::document doc = parser.iterate(json_padded);

    Buffer<osm_RoadNodeList> nodes = BufferAlloc<osm_RoadNodeList>(arena, node_hashmap_size);

    for (auto item : doc["elements"])
    {
        if (item["type"] == "node")
        {
            osm_RoadNode* node = PushStruct(arena, osm_RoadNode);
            node->id = item["id"].get_uint64();
            node->lat = item["lat"].get_double();
            node->lon = item["lon"].get_double();

            U64 node_slot = node->id % node_hashmap_size;
            SLLQueuePush(nodes.data[node_slot].first, nodes.data[node_slot].last, node);
        }
    }

    return nodes;
}

static Buffer<osm_Way>
way_buffer_from_simd_json(Arena* arena, String8 json)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string json_padded((char*)json.str, json.size);
    simdjson::ondemand::document doc = parser.iterate(json_padded);

    U64 way_count = 0;
    for (auto item : doc["elements"])
    {
        if (item["type"] == "way")
        {
            way_count += 1;
        }
    }
    Buffer<osm_Way> way_buffer = BufferAlloc<osm_Way>(arena, way_count);

    U64 way_index = 0;
    for (auto element : doc["elements"])
    {
        if (element["type"] == "way")
        {
            // ~mgj: Insert into hashmap
            U64 way_id = element["id"].get_uint64();
            osm_Way* way = &way_buffer.data[way_index];

            way->id = way_id;
            // Get the nodes array and count elements
            auto nodes_array = element["nodes"].get_array();
            way->node_count = nodes_array.count_elements();

            way->node_ids = PushArray(arena, U64, way->node_count);
            U32 node_index = 0;
            for (auto node_id : nodes_array)
            {
                way->node_ids[node_index] = node_id.get_uint64();
                node_index++;
            }

            // Count tags by iterating through the object
            auto tags_object = element["tags"].get_object();
            U64 tag_count = 0;
            for (auto tag : tags_object)
            {
                tag_count++;
            }

            // Reset and iterate again to store the tags
            tags_object = element["tags"].get_object();
            way->tags = BufferAlloc<osm_Tag>(arena, tag_count);
            U64 tag_cur_index = 0;
            for (auto tag : tags_object)
            {
                // Get key and value as string_view
                std::string_view key_view = tag.unescaped_key();
                std::string_view value_view = tag.value().get_string();

                // Convert to String8
                String8 temp_key = Str8((U8*)key_view.data(), key_view.size());
                String8 temp_value = Str8((U8*)value_view.data(), value_view.size());

                // Copy to arena
                way->tags.data[tag_cur_index].key = PushStr8Copy(arena, temp_key);
                way->tags.data[tag_cur_index].value = PushStr8Copy(arena, temp_value);

                tag_cur_index++;
            }

            way_index++;
        }
    }

    return way_buffer;
}

} // namespace wrapper
