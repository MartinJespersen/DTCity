#include "core_inc.hpp"
#include "osm/osm.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"

namespace wrapper
{

osm::RoadNodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size)
{
    prof_scope_marker;
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

    nodes = buffer_alloc<osm::RoadNodeList>(arena, node_hashmap_size);
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
                osm::WgsNode* node = PushStruct(arena, osm::WgsNode);
                U64 id;
                F64 lat, lon;
                error_num |= item_object.find_field("id").get(id);
                error_num |= item_object.find_field("lat").get_double().get(lat);
                error_num |= item_object.find_field("lon").get_double().get(lon);
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

osm::WayParseResult
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

    way_buffer = buffer_alloc<osm::Way>(arena, way_count);
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
            U64 way_id = 0;
            error = elem["id"].get_uint64().get(way_id);
            if (error) { goto early_ret; }
            osm::Way* way = &way_buffer.data[way_index];

            way->id = way_id;
            // Get the nodes array and count elements
            simdjson::dom::array nodes_array;
            error = elem["nodes"].get_array().get(nodes_array);
            if (error) { goto early_ret; }
            way->node_count = nodes_array.size();

            way->node_ids = PushArray(arena, U64, way->node_count);
            U32 node_index = 0;
            for (auto node_id : nodes_array)
            {
                error = node_id.get_uint64().get(way->node_ids[node_index]);
                if (error) { goto early_ret; }
                node_index++;
            }

            // Count tags by iterating through the object
            simdjson::dom::object tags_object;
            error = elem["tags"].get_object().get(tags_object);
            if (error) { goto early_ret; }
            U64 tag_count = 0;
            for (auto _ : tags_object)
            {
                (void)_;
                tag_count++;
            }

            // Reset and iterate again to store the tags
            way->tags = buffer_alloc<osm::Tag>(arena, tag_count);
            U64 tag_cur_index = 0;
            for (auto tag : tags_object)
            {
                // Get key and value as string_view

                auto key_view = tag.key;
                std::string_view value_view;
                error = tag.value.get_string().get(value_view);
                if (error) { goto early_ret; }

                // Convert to String8
                String8 temp_key = str8((U8*)key_view.data(), key_view.size());
                String8 temp_value = str8((U8*)value_view.data(), value_view.size());

                // Copy to arena
                way->tags.data[tag_cur_index].key = push_str8_copy(arena, temp_key);
                way->tags.data[tag_cur_index].value = push_str8_copy(arena, temp_value);

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
