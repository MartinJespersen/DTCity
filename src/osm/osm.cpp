namespace osm
{
g_internal void
structure_init(U64 node_hashmap_size, U64 way_hashmap_size)
{
    Arena* arena = arena_alloc();
    Buffer<NodeList> node_hashmap = BufferAlloc<NodeList>(arena, node_hashmap_size);
    Buffer<WayList> way_hashmap = BufferAlloc<WayList>(arena, way_hashmap_size);
    Map<NodeId, EcefLocation>* ecef_location_map = map_create<NodeId, EcefLocation>(arena, node_hashmap_size);
    Map<NodeId, WgsLocation>* wgs_location_map = map_create<NodeId, WgsLocation>(arena, node_hashmap_size);

    g_network = PushStruct(arena, Network);
    g_network->arena = arena;
    g_network->node_hashmap = node_hashmap;
    g_network->ecef_location_map = ecef_location_map;
    g_network->wgs_location_map = wgs_location_map;
    g_network->way_hashmap = way_hashmap;
}

g_internal void
structure_cleanup()
{
    arena_release(g_network->arena);
}

g_internal void
_road_edge_structure_create()
{
    prof_scope_marker;
    Buffer<osm::Way> way_buf = g_network->ways_arr[enum_idx(osm::WayType::Highway)];

    ChunkList<RoadEdge>* chunk_list = chunk_list_create<RoadEdge>(g_network->arena, 1024);
    for (const osm::Way& way : way_buf)
    {
        RoadEdge* prev_edge = 0;
        for (U32 node_idx = 1; node_idx < way.node_count; node_idx++)
        {
            U64 prev_node_id = way.node_ids[node_idx - 1];
            U64 node_id = way.node_ids[node_idx];

            RoadEdge* road_edge = chunk_list_get_next(g_network->arena, chunk_list);
            road_edge->id = random_u64();
            road_edge->way_id = way.id;
            road_edge->node_id_from = prev_node_id;
            road_edge->node_id_to = node_id;
            road_edge->prev = prev_edge;

            if (prev_edge)
            {
                prev_edge->next = road_edge;
            }
            prev_edge = road_edge;
        }
    }

    Buffer<RoadEdge> road_edge_buf = buffer_from_chunk_list(g_network->arena, chunk_list);
    Map<S64, RoadEdge*>* road_edge_map = map_create<S64, RoadEdge*>(g_network->arena, 1024);
    for (RoadEdge* edge = road_edge_buf.begin(); edge < road_edge_buf.end(); edge++)
    {
        map_insert(road_edge_map, edge->id, edge);
    }

    g_network->edge_structure = {.edges = road_edge_buf, .edge_map = *road_edge_map};
}

g_internal std::shared_ptr<async::AsyncTaskState<OsmTaskState>>
_osm_task_state_create(String8 file, String8 bbox_str)
{
    std::shared_ptr<async::AsyncTaskState<OsmTaskState>> osm_task_state = async::async_task_state_create<OsmTaskState>(S("osm task"));
    osm_task_state->task_state.file = push_str8_copy(osm_task_state->arena, file);
    osm_task_state->task_state.bbox_str = push_str8_copy(osm_task_state->arena, bbox_str);

    return osm_task_state;
}

g_internal void
_parse_osm_data(async::ThreadInfo info, void* data)
{
    (void)info;
    ScratchScope scratch = ScratchScope(0, 0);
    OsmTaskState* task_state = (OsmTaskState*)data;

    RoadNodeParseResult node_result = wrapper::node_buffer_from_simd_json(g_network->arena, task_state->body, 1000);
    if (node_result.error)
    {
        DEBUG_LOG("Error happend when parsing nodes from json");
    }
    WayParseResult osm_way_parse_result = wrapper::way_buffer_from_simd_json(g_network->arena, task_state->body);
    if (osm_way_parse_result.error)
    {
        DEBUG_LOG("Error happend when parsing ways from json");
    }

    Buffer<Way> ways = osm_way_parse_result.ways;
    for (U32 way_type_idx = 0; way_type_idx < enum_idx(WayType::Count); ++way_type_idx)
    {
        ChunkList<Way>* way_chunk_list = chunk_list_create<Way>(scratch.arena, 1024);
        ChunkList<NodeId>* node_id_chunk_list = chunk_list_create<NodeId>(scratch.arena, 1024);
        for (U32 way_index = 0; way_index < ways.size; way_index++)
        {
            Way* way = &ways.data[way_index];
            TagResult tag_result = tag_find(scratch.arena, way->tags, S(g_waytype_osm_tag[way_type_idx]));
            if (enum_idx(tag_result.result))
            {
                continue;
            }

            chunk_list_insert(scratch.arena, way_chunk_list, *way);
            for (U32 node_index = 0; node_index < way->node_count; node_index++)
            {
                U64 node_id = way->node_ids[node_index];
                Node* node_utm;
                B8 inserted = _node_hashmap_insert(node_id, way, &node_utm);
                if (inserted)
                {
                    WgsNode* node_coord = _wgs_node_find(node_result.road_nodes, node_id);

                    // Cartographic to ECEF transformation
                    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(node_coord->lon), glm::radians(node_coord->lat), 0);
                    glm::dvec3 coord_ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(origin_cartographic);
                    EcefLocation loc = ecef_location_create(node_id, vec_3f64(coord_ecef.x, coord_ecef.y, coord_ecef.z));
                    WgsLocation wgs_loc = {.id = node_id, .lat = node_coord->lat, .lon = node_coord->lon};

                    map_insert(g_network->ecef_location_map, node_id, loc);
                    map_insert(g_network->wgs_location_map, node_id, wgs_loc);

                    chunk_list_insert(scratch.arena, node_id_chunk_list, node_id);
                }
            }
        }

        g_network->node_id_arr[way_type_idx] = buffer_from_chunk_list(g_network->arena, node_id_chunk_list);
        g_network->ways_arr[way_type_idx] = buffer_from_chunk_list(g_network->arena, way_chunk_list);
    }

    _road_edge_structure_create();
    g_network->network_ready.store(true);
}

g_internal async::UserFuncResult<OsmTaskState>
_cache_and_parse_osm_json(Arena* arena, async::ThreadPool* thread_pool, String8 response_body, OsmTaskState* task_state)
{
    (void)task_state;
    (void)thread_pool;
    ScratchScope scratch = ScratchScope(&arena, 1);
    cache_write(task_state->file, response_body, task_state->bbox_str);
    task_state->body = response_body;
    async::WorkerTask work_task = {.data = task_state, .worker_func = _parse_osm_data};
    thread_pool_push(thread_pool, &work_task);
    // parse nodes
    // ~mgj: parse OSM way structures
    return async::UserFuncResult<OsmTaskState>().success();
}

g_internal async::AsyncHttpTaskCreateResult<OsmTaskState>
async_structure_create(Rng2F64 bbox)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Context* ctx = dt_ctx_get();

    String8List query_list = {};
    for (U32 way_type_idx = 0; way_type_idx < enum_idx(WayType::Count); ++way_type_idx)
    {
        String8 str = PushStr8F(scratch.arena, "way[\"%s\"](%f, %f, %f, %f);\n", g_waytype_osm_tag[way_type_idx], bbox.min.y, bbox.min.x, bbox.max.y, bbox.max.x);
        str8_list_push(scratch.arena, &query_list, str);
    }

    String8 dyn_query_str = str8_list_join(scratch.arena, &query_list, 0);
    // fetch osm data from overpass api
    String8 body = push_str8f(scratch.arena, R"(data=
            [out:json] [timeout:25];
            (
                %.*s
            );
            out body;
            >;
            out skel qt;
        )",
                              str8_varg(dyn_query_str));

    String8 file = str8_path_from_str8_list(scratch.arena, {ctx->data_subdirs.data[dt_DataDirType::Cache], S("osm_data.json")});
    String8 path = S("http://overpass-api.de/api/interpreter");
    String8 bbox_str = Str8(reinterpret_cast<U8*>(&bbox), sizeof(bbox));
    std::shared_ptr<async::AsyncTaskState<OsmTaskState>> osm_task_state = _osm_task_state_create(file, bbox_str);
    async::AsyncHttpTaskCreateResult<OsmTaskState> result = {.task_state = osm_task_state};
    Result<String8> cache_result = cache_read(g_network->arena, file, bbox_str);
    if (cache_result.err)
    {
        result.async_result = async::async_http_task_create(osm_task_state, ctx->thread_pool, HTTP_Method_Post, path, S("text/html"), body, {}, {}, _cache_and_parse_osm_json, 3, 1);
    }
    else
    {
        osm_task_state->task_state.body = cache_result.v;
        async::WorkerTask work_task = {.data = &osm_task_state->task_state, .worker_func = _parse_osm_data};
        thread_pool_push(ctx->thread_pool, &work_task);
        osm_task_state->success.store(true);
        osm_task_state->done.store(true);
    }

    return result;
}

g_internal WgsNode*
_wgs_node_find(Buffer<RoadNodeList> node_hashmap, U64 node_id)
{
    RoadNodeList* node_list = &node_hashmap.data[node_id % node_hashmap.size];

    WgsNode* node = node_list->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

g_internal WayNode*
way_find(WayId way_id)
{
    Network* network = g_network;
    WayList* way_list = &network->way_hashmap.data[way_id % network->way_hashmap.size];

    WayNode* node = way_list->first;
    for (; node; node = node->hash_next)
    {
        Way* way = &node->way;
        if (way->id == way_id)
        {
            break;
        }
    }
    return node;
}

g_internal TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find)
{
    TagResult result = {};
    result.result = TagResultEnum::ROAD_TAG_NOT_FOUND;
    for (U32 i = 0; i < tags.size; i++)
    {
        if (Str8Cmp(tags.data[i].key, tag_to_find))
        {
            result.result = TagResultEnum::ROAD_TAG_FOUND;
            result.value = push_str8_copy(arena, tags.data[i].value);
            break;
        }
    }
    return result;
}

g_internal EcefLocation
location_get(NodeId node_id)
{
    prof_scope_marker;
    EcefLocation* loc = map_get(g_network->ecef_location_map, node_id);
    if (loc)
    {
        return *loc;
    }
    Assert("Node not found in hash map");
    return {};
}

g_internal WgsLocation
wgs_location_get(NodeId node_id)
{
    prof_scope_marker;
    WgsLocation* loc = map_get(g_network->wgs_location_map, node_id);
    if (loc)
    {
        return *loc;
    }
    Assert("Node not found in hash map");
    return {};
}

g_internal Node*
node_get(NodeId node_id)
{
    Buffer<NodeList> utm_node_hashmap = g_network->node_hashmap;
    U32 slot_index = node_id % utm_node_hashmap.size;
    NodeList* slot = &utm_node_hashmap.data[slot_index];
    for (Node* node = slot->first; node; node = node->next)
    {
        if (node->id == node_id)
        {
            return node;
        }
    }
    Assert(0 && "Node not found in hash map");
    return &g_road_node_utm;
}

g_internal B32
_node_hashmap_insert(NodeId node_id, Way* way, Node** out)
{
    Network* network = g_network;

    // ~mgj: Insert Node into hash if not already inserted
    U64 node_slot = node_id % network->node_hashmap.size;
    NodeList* slot = &network->node_hashmap.data[node_slot];
    Node* node = slot->first;
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
        node = PushStruct(network->arena, Node);
        node->id = node_id;
        SLLQueuePush(slot->first, slot->last, node);
        Assert(slot->last->next == 0);
        node_inserted = 1;
    }

    WayNode* way_node = PushStruct(network->arena, WayNode);
    way_node->way = *way;

    // ~mgj: every node should be quickly able to look up the roadways it is part of
    SLLQueuePush(node->way_queue.first, node->way_queue.last, way_node);

    // ~mgj: Ways are put into its hashmap for quick lookup (relevant for pixel picking operations)
    U64 way_id = way->id;
    U64 way_slot = way_id % network->way_hashmap.size;
    WayList* way_list = &network->way_hashmap.data[way_slot];
    SLLQueuePush_N(way_list->first, way_list->last, way_node, hash_next);

    *out = node;
    return node_inserted;
}

g_internal NodeId
random_node_id_from_type_get(WayType type)
{
    Network* network = g_network;

    Buffer<NodeId> node_ids = network->node_id_arr[enum_idx(type)];
    Assert(node_ids.size);
    NodeId node_id = node_ids.data[random_u64() % node_ids.size];

    return node_id;
}

g_internal Node*
random_neighbour_node_get(U64 node_id)
{
    Node* node = node_get(node_id);
    Node* neighbour = random_neighbour_node_get(node);
    return neighbour;
}

g_internal Node*
random_neighbour_node_get(Node* node)
{
    // Calculate roadway count for the node
    U32 roadway_count = 0;
    for (WayNode* way_element = node->way_queue.first; way_element; way_element = way_element->next)
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
    WayNode* way_node = node->way_queue.first;
    for (U32 i = 0; i < rand_roadway_idx; ++i)
    {
        way_node = way_node->next;
    }
    Assert(way_node);

    Way* way = &way_node->way;
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
    Node* next_node = node_get(next_node_id);
    return next_node;
}

} // namespace osm
