
g_internal void
osm::structure_init(U64 node_hashmap_size, U64 way_hashmap_size, Rng2F64 utm_coords)
{
    Arena* arena = ArenaAlloc();
    Buffer<osm::NodeList> utm_node_hashmap = BufferAlloc<osm::NodeList>(arena, node_hashmap_size);
    Buffer<osm::WayList> way_hashmap = BufferAlloc<osm::WayList>(arena, way_hashmap_size);
    Map<osm::NodeId, osm::UtmLocation>* utm_location_map =
        map_create<osm::NodeId, osm::UtmLocation>(arena, node_hashmap_size);

    F64 center_transform_x = -(utm_coords.min.x + utm_coords.max.x) / 2.0;
    F64 center_transform_y = -(utm_coords.min.y + utm_coords.max.y) / 2.0;
    Vec2F64 utm_center_offset = {center_transform_x, center_transform_y};

    osm::g_network = PushStruct(arena, osm::Network);
    *osm::g_network = {arena, utm_node_hashmap, utm_center_offset, utm_location_map, way_hashmap};
}

g_internal void
osm::structure_cleanup()
{
    ArenaRelease(osm::g_network->arena);
}

g_internal void
osm::structure_add(Buffer<osm::RoadNodeList> node_hashmap, String8 json, osm::WayType osm_key_type)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    osm::Network* network = osm::g_network;
    Arena* arena = network->arena;
    // ~mgj: parse OSM way structures
    osm::WayParseResult osm_way_parse_result =
        wrapper::way_buffer_from_simd_json(network->arena, json);

    if (osm_way_parse_result.error)
    {
        DEBUG_LOG("Failed to parse OSM way structures\n");
    }

    Buffer<osm::Way> ways = osm_way_parse_result.ways;
    ChunkList<NodeId>* node_id_chunk_list = chunk_list_create<NodeId>(arena, 1024);

    for (U32 way_index = 0; way_index < ways.size; way_index++)
    {
        osm::Way* way = &ways.data[way_index];
        for (U32 node_index = 0; node_index < way->node_count; node_index++)
        {
            U64 node_id = way->node_ids[node_index];
            osm::Node* node_utm;
            B8 inserted = osm::node_hashmap_insert(node_id, way, &node_utm);
            if (inserted)
            {
                osm::WgsNode* node_coord = osm::wgs_node_find(node_hashmap, node_id);
                double x, y;
                char node_utm_zone[10];
                UTM::LLtoUTM(node_coord->lat, node_coord->lon, y, x, node_utm_zone);

                String8 utm_zone_str = str8_c_string(node_utm_zone);
                node_utm->utm_zone = push_str8_copy(arena, utm_zone_str);

                UtmLocation loc = utm_location_create(node_id, vec_2f32(x, y));
                map_insert(network->utm_location_map, node_id, loc);

                chunk_list_insert(scratch.arena, node_id_chunk_list, node_id);
            }
        }
    }

    network->node_id_arr[osm_key_type] = buffer_from_chunk_list(arena, node_id_chunk_list);
    network->ways_arr[osm_key_type] = ways;
}

g_internal osm::WgsNode*
osm::wgs_node_find(Buffer<osm::RoadNodeList> node_hashmap, U64 node_id)
{
    osm::RoadNodeList* node_list = &node_hashmap.data[node_id % node_hashmap.size];

    osm::WgsNode* node = node_list->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

g_internal osm::WayNode*
osm::way_find(U64 way_id)
{
    osm::Network* network = osm::g_network;
    osm::WayList* way_list = &network->way_hashmap.data[way_id % network->way_hashmap.size];

    osm::WayNode* node = way_list->first;
    for (; node; node = node->hash_next)
    {
        osm::Way* way = &node->way;
        if (way->id == way_id)
        {
            break;
        }
    }
    return node;
}

g_internal osm::TagResult
osm::tag_find(Arena* arena, Buffer<osm::Tag> tags, String8 tag_to_find)
{
    osm::TagResult result = {};
    result.result = osm::TagResultEnum::ROAD_TAG_NOT_FOUND;
    for (U32 i = 0; i < tags.size; i++)
    {
        if (Str8Cmp(tags.data[i].key, tag_to_find))
        {
            result.result = osm::TagResultEnum::ROAD_TAG_FOUND;
            result.value = push_str8_copy(arena, tags.data[i].value);
            break;
        }
    }
    return result;
}

g_internal osm::UtmLocation
osm::utm_location_get(NodeId node_id)
{
    prof_scope_marker;
    UtmLocation* loc = map_get(osm::g_network->utm_location_map, node_id);
    if (loc)
    {
        return *loc;
    }
    Assert(0 && "Node not found in hash map");
    return {};
}

g_internal osm::Node*
osm::node_get(NodeId node_id)
{
    Buffer<NodeList> utm_node_hashmap = osm::g_network->utm_node_hashmap;
    U32 slot_index = node_id % utm_node_hashmap.size;
    osm::NodeList* slot = &utm_node_hashmap.data[slot_index];
    for (osm::Node* node = slot->first; node; node = node->next)
    {
        if (node->id == node_id)
        {
            return node;
        }
    }
    Assert(0 && "Node not found in hash map");
    return &osm::g_road_node_utm;
}

g_internal B32
osm::node_hashmap_insert(NodeId node_id, osm::Way* way, osm::Node** out)
{
    osm::Network* network = osm::g_network;

    // ~mgj: Insert Node into hash if not already inserted
    U64 node_slot = node_id % network->utm_node_hashmap.size;
    osm::NodeList* slot = &network->utm_node_hashmap.data[node_slot];
    osm::Node* node = slot->first;
    B32 node_inserted = false;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    if (!node)
    {
        node = PushStruct(network->arena, osm::Node);
        node->id = node_id;
        SLLQueuePush(slot->first, slot->last, node);
        Assert(slot->last->next == 0);
        node_inserted = 1;
    }

    osm::WayNode* way_node = PushStruct(network->arena, osm::WayNode);
    way_node->way = *way;

    // ~mgj: every node should be quickly able to look up the roadways it is part of
    SLLQueuePush(node->way_queue.first, node->way_queue.last, way_node);

    // ~mgj: Ways are put into its hashmap for quick lookup (relevant for pixel picking operations)
    U64 way_id = way->id;
    U64 way_slot = way_id % network->way_hashmap.size;
    osm::WayList* way_list = &network->way_hashmap.data[way_slot];
    SLLQueuePush_N(way_list->first, way_list->last, way_node, hash_next);

    *out = node;
    return node_inserted;
}

g_internal osm::NodeId
osm::random_node_id_from_type_get(osm::WayType type)
{
    Network* network = g_network;

    Buffer<NodeId> node_ids = network->node_id_arr[type];
    NodeId node_id = node_ids.data[random_u64() % node_ids.size];

    return node_id;
}

g_internal osm::Node*
osm::random_neighbour_node_get(U64 node_id)
{
    osm::Node* node = osm::node_get(node_id);
    osm::Node* neighbour = osm::random_neighbour_node_get(node);
    return neighbour;
}

g_internal osm::Node*
osm::random_neighbour_node_get(osm::Node* node)
{
    // Calculate roadway count for the node
    U32 roadway_count = 0;
    for (osm::WayNode* way_element = node->way_queue.first; way_element;
         way_element = way_element->next)
    {
        roadway_count++;
    }
    Assert(roadway_count > 0);

    // Find random roadway
    if (roadway_count == 0)
    {
        return nullptr;
    }

    U32 rand_num = random_u32();
    U32 rand_roadway_idx = rand_num % roadway_count;
    osm::WayNode* way_node = node->way_queue.first;
    for (U32 i = 0; i < rand_roadway_idx; ++i)
    {
        way_node = way_node->next;
    }
    Assert(way_node);

    osm::Way* way = &way_node->way;
    U32 node_idx = 0;
    for (; node_idx < way->node_count; node_idx++)
    {
        if (way->node_ids[node_idx] == node->id)
        {
            break;
        }
    }
    Assert(node_idx < way->node_count);

    U32 next_node_idx = (node_idx + 1);
    if (next_node_idx >= way->node_count)
    {
        next_node_idx = Max(0, (S32)node_idx - 1);
    }
    U64 next_node_id = way->node_ids[next_node_idx];
    osm::Node* next_node = osm::node_get(next_node_id);
    return next_node;
}
