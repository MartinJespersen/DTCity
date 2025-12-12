
static void
osm_structure_init(U64 node_hashmap_size, U64 way_hashmap_size, Rng2F64 utm_coords)
{
    Arena* arena = ArenaAlloc();
    Buffer<osm_UtmNodeList> utm_node_hashmap =
        BufferAlloc<osm_UtmNodeList>(arena, node_hashmap_size);
    Buffer<osm_WayList> way_hashmap = BufferAlloc<osm_WayList>(arena, way_hashmap_size);

    F64 center_transform_x = -(utm_coords.min.x + utm_coords.max.x) / 2.0;
    F64 center_transform_y = -(utm_coords.min.y + utm_coords.max.y) / 2.0;
    Vec2F64 utm_center_offset = {center_transform_x, center_transform_y};

    osm_g_network = PushStruct(arena, osm_Network);
    *osm_g_network = {arena, utm_node_hashmap, utm_center_offset, way_hashmap};
}

static void
osm_structure_cleanup()
{
    ArenaRelease(osm_g_network->arena);
}

static void
osm_structure_add(Buffer<osm_RoadNodeList> node_hashmap, String8 json, osm_KeyType osm_key_type)
{
    osm_Network* network = osm_g_network;
    Arena* arena = network->arena;
    // ~mgj: parse OSM way structures
    osm_WayParseResult osm_way_parse_result =
        wrapper::way_buffer_from_simd_json(network->arena, json);

    if (osm_way_parse_result.error)
    {
        DEBUG_LOG("Failed to parse OSM way structures\n");
    }

    Buffer<osm_Way> ways = osm_way_parse_result.ways;
    for (U32 way_index = 0; way_index < ways.size; way_index++)
    {
        osm_Way* way = &ways.data[way_index];
        for (U32 node_index = 0; node_index < way->node_count; node_index++)
        {
            U64 node_id = way->node_ids[node_index];
            osm_UtmNode* node_utm;
            B8 inserted = osm_node_hashmap_insert(node_id, way, &node_utm);
            if (inserted)
            {
                osm_RoadNode* node_coord = osm_node_find(node_hashmap, node_id);
                double x, y;
                char node_utm_zone[10];
                UTM::LLtoUTM(node_coord->lat, node_coord->lon, y, x, node_utm_zone);

                String8 utm_zone_str = str8_c_string(node_utm_zone);
                node_utm->utm_zone = PushStr8Copy(arena, utm_zone_str);
                node_utm->pos.x = x + network->utm_center_offset.x;
                node_utm->pos.y = y + network->utm_center_offset.y;
            }
        }
    }

    network->ways_arr[osm_key_type] = ways;
}
static osm_RoadNode*
osm_node_find(Buffer<osm_RoadNodeList> node_hashmap, U64 node_id)
{
    osm_RoadNodeList* node_list = &node_hashmap.data[node_id % node_hashmap.size];

    osm_RoadNode* node = node_list->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

static osm_WayNode*
osm_way_find(U64 way_id)
{
    osm_Network* network = osm_g_network;
    osm_WayList* way_list = &network->way_hashmap.data[way_id % network->way_hashmap.size];

    osm_WayNode* node = way_list->first;
    for (; node; node = node->hash_next)
    {
        osm_Way* way = &node->way;
        if (way->id == way_id)
        {
            break;
        }
    }
    return node;
}

static osm_TagResult
osm_tag_find(Arena* arena, Buffer<osm_Tag> tags, String8 tag_to_find)
{
    osm_TagResult result = {};
    result.result = ROAD_TAG_NOT_FOUND;
    for (U32 i = 0; i < tags.size; i++)
    {
        if (Str8Cmp(tags.data[i].key, tag_to_find))
        {
            result.result = ROAD_TAG_FOUND;
            result.value = PushStr8Copy(arena, tags.data[i].value);
            break;
        }
    }
    return result;
}

static osm_UtmNode*
osm_random_utm_node_get()
{
    Buffer<osm_UtmNodeList> utm_node_hashmap = osm_g_network->utm_node_hashmap;
    U32 rand_num = RandomU32();
    for (U32 i = 0; i < utm_node_hashmap.size; ++i)
    {
        U32 slot_index = rand_num % utm_node_hashmap.size;
        osm_UtmNodeList* slot = &utm_node_hashmap.data[slot_index];
        if (slot->first != NULL)
        {
            return slot->first;
        }
        rand_num++;
    }
    return &osm_g_road_node_utm;
}

static osm_UtmNode*
osm_utm_node_find(U64 node_id)
{
    Buffer<osm_UtmNodeList> utm_node_hashmap = osm_g_network->utm_node_hashmap;
    U32 slot_index = node_id % utm_node_hashmap.size;
    osm_UtmNodeList* slot = &utm_node_hashmap.data[slot_index];
    for (osm_UtmNode* node = slot->first; node; node = node->next)
    {
        if (node->id == node_id)
        {
            return node;
        }
    }
    return &osm_g_road_node_utm;
}

static B32
osm_node_hashmap_insert(U64 node_id, osm_Way* way, osm_UtmNode** out)
{
    osm_Network* structure = osm_g_network;

    // ~mgj: Insert Node into hash if not already inserted
    U64 node_slot = node_id % structure->utm_node_hashmap.size;
    osm_UtmNodeList* slot = &structure->utm_node_hashmap.data[node_slot];
    osm_UtmNode* node = slot->first;
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
        node = PushStruct(structure->arena, osm_UtmNode);
        node->id = node_id;
        SLLQueuePush(slot->first, slot->last, node);
        Assert(slot->last->next == 0);
        node_inserted = 1;
    }

    osm_WayNode* road_way_element = PushStruct(structure->arena, osm_WayNode);
    road_way_element->way = *way;

    // ~mgj: every node should be quickly able to look up its ways it is part of it
    SLLQueuePush(node->way_queue.first, node->way_queue.last, road_way_element);

    // ~mgj: Ways are put into its hashmap for quick lookup (relevant for pixel picking operations)
    U64 way_id = way->id;
    U64 way_slot = way_id % structure->way_hashmap.size;
    osm_WayList* way_list = &structure->way_hashmap.data[way_slot];
    SLLQueuePush_N(way_list->first, way_list->last, road_way_element, hash_next);

    *out = node;
    return node_inserted;
}

static osm_UtmNode*
osm_random_neighbour_node_get(osm_UtmNode* node)
{
    // Calculate roadway count for the node
    U32 roadway_count = 0;
    for (osm_WayNode* way_element = node->way_queue.first; way_element;
         way_element = way_element->next)
    {
        roadway_count++;
    }
    osm_WayNode* way_element = node->way_queue.first;

    // Find random roadway
    U32 rand_num = RandomU32();
    if (roadway_count == 0)
    {
        return &osm_g_road_node_utm;
    }

    U32 rand_roadway_idx = rand_num % roadway_count;
    for (U32 i = 0; i < rand_roadway_idx; ++i)
    {
        way_element = way_element->next;
    }
    Assert(way_element);

    osm_Way* way = &way_element->way;
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
    osm_UtmNode* next_node = osm_utm_node_find(next_node_id);
    return next_node;
}
