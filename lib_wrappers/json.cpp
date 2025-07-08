namespace wrapper
{
static void
OverpassHighways(Arena* arena, city::Road* roads, String8 json)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string json_padded((char*)json.str,
                                        json.size); // load JSON file 'twitter.json'.
    simdjson::ondemand::document doc =
        parser.iterate(json_padded); // position a pointer at the beginning of the JSON data

    roads->nodes = PushArray(arena, city::RoadNodeSlot, roads->node_slot_count);

    U64 way_count = 0;
    for (auto item : doc["elements"])
    {
        if (item["type"] == "node")
        {
            city::RoadNode* node = PushStruct(arena, city::RoadNode);
            node->id = item["id"].get_uint64();
            node->lat = item["lat"].get_double();
            node->lon = item["lon"].get_double();

            U64 map_location = node->id % roads->node_slot_count;
            SLLQueuePush(roads->nodes[map_location].first, roads->nodes[map_location].last, node);
        }
        else if (item["type"] == "way")
        {
            way_count++;
        }
    }

    roads->way_count = way_count;
    roads->ways = PushArray(arena, city::RoadWay, roads->way_count);

    U64 way_index = 0;
    for (auto element : doc["elements"])
    {
        if (element["type"] == "way")
        {
            city::RoadWay* way = &roads->ways[way_index];
            way->id = element["id"].get_uint64();

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
            way->tag_count = 0;
            for (auto tag : tags_object)
            {
                way->tag_count++;
            }

            // Reset and iterate again to store the tags
            tags_object = element["tags"].get_object();
            way->tags = PushArray(arena, city::RoadTags, way->tag_count);
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
                way->tags[tag_cur_index].key = PushStr8Copy(arena, temp_key);
                way->tags[tag_cur_index].value = PushStr8Copy(arena, temp_value);

                tag_cur_index++;
            }

            way_index++;
        }
    }
}
} // namespace wrapper
