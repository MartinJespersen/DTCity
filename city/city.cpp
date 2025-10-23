namespace city
{
static Road*
RoadCreate(String8 texture_path, String8 cache_path, GCSBoundingBox* gcs_bbox,
           R_SamplerInfo* sampler_info)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();

    Road* road = PushStruct(arena, Road);

    road->openapi_data_file_name = PushStr8Copy(arena, S("openapi_node_ways_highway.json"));
    road->arena = arena;
    road->road_height = 10.0f;
    road->default_road_width = 2.0f;

    // TODO: make this an input and check for input conditions
    String8 query = S(R"(data=
        [out:json] [timeout:25];
        (
          way["highway"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )");
    String8 content =
        DataFetch(scratch.arena, cache_path, road->openapi_data_file_name, query, gcs_bbox);
    wrapper::OverpassNodeWayParse(road->arena, content, 100, &road->node_ways);
    NodeWays* node_ways = &road->node_ways;

    // ~mgj: Road Decision tree create
    U64 hashmap_slot_count = 100;
    NodeStructureCreate(road->arena, node_ways, gcs_bbox, hashmap_slot_count,
                        &road->node_utm_structure);

    RoadVertexBufferCreate(road, &road->vertex_buffer, &road->index_buffer);
    R_BufferInfo vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(road->vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo index_buffer_info =
        R_BufferInfoFromTemplateBuffer(road->index_buffer, R_BufferType_Index);

    road->texture_path = Str8PathFromStr8List(road->arena, {texture_path, S("road_texture.ktx2")});

    road->texture_handle = R_TextureLoad(sampler_info, road->texture_path, R_PipelineUsageType_3D);
    road->vertex_handle = R_BufferLoad(&vertex_buffer_info);
    road->index_handle = R_BufferLoad(&index_buffer_info);

    return road;
}

static void
RoadDestroy(Road* road)
{
    VK_AssetManagerBufferFree(road->vertex_handle);
    VK_AssetManagerBufferFree(road->index_handle);
    VK_AssetManagerTextureFree(road->texture_handle);

    ArenaRelease(road->arena);
}

static inline RoadNode*
NodeFind(NodeWays* node_ways, U64 node_id)
{
    RoadNodeSlot node_slot = node_ways->nodes[node_id % node_ways->node_slot_count];

    RoadNode* node = node_slot.first;
    for (; node <= node_slot.last; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

static void
RoadSegmentFromTwoRoadNodes(RoadSegment* out_road_segment, NodeUtm* node_0, NodeUtm* node_1,
                            F32 road_width)
{
    Vec2F32 road_0_pos = node_0->pos;
    Vec2F32 road_1_pos = node_1->pos;

    Vec2F32 road_dir = Sub2F32(road_1_pos, road_0_pos);
    Vec2F32 orthogonal_vec = {road_dir.y, -road_dir.x};
    Vec2F32 normal = Normalize2F32(orthogonal_vec);
    Vec2F32 normal_scaled = Scale2F32(normal, road_width / 2.0f);

    out_road_segment->start.top = Add2F32(road_0_pos, normal_scaled);
    out_road_segment->start.btm = Sub2F32(road_0_pos, normal_scaled);
    out_road_segment->end.top = Add2F32(road_1_pos, normal_scaled);
    out_road_segment->end.btm = Sub2F32(road_1_pos, normal_scaled);

    out_road_segment->start.node = node_0;
    out_road_segment->end.node = node_1;
}

// find the two nodes connecting two road segments
// source: https://opensage.github.io/blog/roads-how-boring-part-5-connecting-the-road-segments
static void
RoadSegmentConnectionFromTwoRoadSegments(RoadSegment* in_out_road_segment_0,
                                         RoadSegment* in_out_road_segment_1, F32 road_width)
{
    // find a single node connecting two road segments for both the top and bottom of the road
    // segments
    Vec2F32 road0_top = in_out_road_segment_0->end.top;
    Vec2F32 road1_top = in_out_road_segment_1->start.top;
    Vec2F32 shared_center = in_out_road_segment_0->end.node->pos;

    Vec2F32 road0_top_dir_norm = Normalize2F32(Sub2F32(road0_top, shared_center));
    Vec2F32 road1_top_dir_norm = Normalize2F32(Sub2F32(road1_top, shared_center));
    // take average of the two directions and normalize it
    Vec2F32 shared_top_normal =
        Normalize2F32(Div2F32(Add2F32(road0_top_dir_norm, road1_top_dir_norm), 2.0f));
    F32 cos_angle = Dot2F32(shared_top_normal, road1_top_dir_norm);
    F32 shared_top_len = (road_width / 2.0f) / cos_angle;

    Vec2F32 shared_top_dir = Scale2F32(shared_top_normal, shared_top_len);
    Vec2F32 shared_top = Add2F32(shared_center, shared_top_dir);
    Vec2F32 shared_btm = Sub2F32(shared_center, shared_top_dir);

    in_out_road_segment_0->end.btm = shared_btm;
    in_out_road_segment_0->end.top = shared_top;

    in_out_road_segment_1->start.btm = shared_btm;
    in_out_road_segment_1->start.top = shared_top;
}

static TagResult
TagFind(Arena* arena, Buffer<Tag> tags, String8 tag_to_find)
{
    TagResult result = {};
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

static B32
UniqueNodeAndWayInsert(Arena* arena, U64 node_id, Way* road_way, Buffer<NodeUtmSlot> hashmap,
                       NodeUtm** out)
{
    U64 index = node_id % hashmap.size;
    NodeUtmSlot* slot = &hashmap.data[index];
    NodeUtm* node = slot->first;
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
        node = PushStruct(arena, NodeUtm);
        node->id = node_id;
        SLLQueuePush(slot->first, slot->last, node);
        node_inserted = 1;
    }

    WayNode* road_way_element = PushStruct(arena, WayNode);
    road_way_element->road_way = road_way;

    SLLQueuePush(node->way_queue.first, node->way_queue.last, road_way_element);

    *out = node;
    return node_inserted;
}

static void
NodeStructureCreate(Arena* arena, NodeWays* node_ways, GCSBoundingBox* gcs_bbox,
                    U64 hashmap_slot_count, NodeUtmStructure* out_node_utm_structure)
{
    out_node_utm_structure->node_hashmap_size = hashmap_slot_count;

    F64 long_low_utm;
    F64 lat_low_utm;
    F64 long_high_utm;
    F64 lat_high_utm;
    char utm_zone[10];
    UTM::LLtoUTM(gcs_bbox->lat_btm_left, gcs_bbox->lon_btm_left, lat_low_utm, long_low_utm,
                 utm_zone);
    UTM::LLtoUTM(gcs_bbox->lat_top_right, gcs_bbox->lon_top_right, lat_high_utm, long_high_utm,
                 utm_zone);
    F64 center_transform_x = -(long_low_utm + long_high_utm) / 2.0;
    F64 center_transform_y = -(lat_low_utm + lat_high_utm) / 2.0;
    out_node_utm_structure->utm_center_offset = {center_transform_x, center_transform_y};

    out_node_utm_structure->node_hashmap =
        BufferAlloc<NodeUtmSlot>(arena, out_node_utm_structure->node_hashmap_size);
    for (U32 way_index = 0; way_index < node_ways->ways.size; way_index++)
    {
        Way* way = &node_ways->ways.data[way_index];
        for (U32 node_index = 0; node_index < way->node_count; node_index++)
        {
            U64 node_id = way->node_ids[node_index];
            NodeUtm* node_utm;
            UniqueNodeAndWayInsert(arena, node_id, way, out_node_utm_structure->node_hashmap,
                                   &node_utm);
            RoadNode* node_coord = NodeFind(node_ways, node_id);
            double x, y;

            char node_utm_zone[10];
            UTM::LLtoUTM(node_coord->lat, node_coord->lon, y, x, node_utm_zone);

            String8 utm_zone_str = Str8CString(node_utm_zone);
            node_utm->utm_zone = PushStr8Copy(arena, utm_zone_str);
            node_utm->pos.x = x + out_node_utm_structure->utm_center_offset.x;
            node_utm->pos.y = y + out_node_utm_structure->utm_center_offset.y;
        }
    }
}

static F32
TagValueF32Get(Arena* arena, String8 key, F32 default_width, Buffer<Tag> tags)
{
    F32 road_width = default_width; // Example value, adjust as needed
    {
        TagResult result = TagFind(arena, tags, key);
        if (result.result == TagResultEnum::ROAD_TAG_FOUND)
        {
            F32 float_result = {0};
            if (F32FromStr8(result.value, &float_result))
            {
                road_width = float_result;
            }
        }
    }
    return road_width;
}

static void
RoadVertexBufferCreate(Road* road, Buffer<Vertex3D>* out_vertex_buffer,
                       Buffer<U32>* out_index_buffer)
{
    ScratchScope scratch = ScratchScope(0, 0);
    NodeWays* node_ways = &road->node_ways;

    U64 total_road_segment_count = 0;
    for (U32 way_index = 0; way_index < node_ways->ways.size; way_index++)
    {
        Way* way = &node_ways->ways.data[way_index];
        // -1 get the number of road segments (because the first and last vertices are shared
        // between segments)
        total_road_segment_count += way->node_count - 1;
    }

    // 6 vertices per road segment quad
    U64 total_vert_count = total_road_segment_count * 4;
    U64 total_index_count = total_road_segment_count * 6;

    Buffer<Vertex3D> vertex_buffer = BufferAlloc<Vertex3D>(road->arena, total_vert_count);
    Buffer<U32> index_buffer = BufferAlloc<U32>(road->arena, total_index_count);

    U32 current_vertex_idx = 0;
    U32 current_index_idx = 0;
    for (U32 way_index = 0; way_index < road->node_ways.ways.size; way_index++)
    {
        Way* way = &road->node_ways.ways.data[way_index];
        //
        // ~mgj: road width calculation
        F32 road_width =
            TagValueF32Get(scratch.arena, S("width"), road->default_road_width, way->tags);
        Assert(road_width > 0.0f);

        if (way->node_count < 2)
        {
            ExitWithError("expected at least one road segment comprising of two nodes");
        }

        U64 node_first_id = way->node_ids[0];
        U64 node_second_id = way->node_ids[1];

        NodeUtm* node_first = UtmNodeFind(&road->node_utm_structure, node_first_id);
        NodeUtm* node_second = UtmNodeFind(&road->node_utm_structure, node_second_id);

        RoadSegment road_segment_prev;
        RoadSegmentFromTwoRoadNodes(&road_segment_prev, node_first, node_second, road_width);

        if (way->node_count == 2)
        {
            // RoadIntersectionPointsFind(road, &road_segment_prev, way);
            QuadToBufferAdd(&road_segment_prev, vertex_buffer, index_buffer, way->id,
                            road->road_height, &current_vertex_idx, &current_index_idx);
        }
        else
        {
            for (U32 node_idx = 1; node_idx < way->node_count - 1; node_idx++)
            {
                U64 node1_id = way->node_ids[node_idx];
                U64 node2_id = way->node_ids[node_idx + 1];

                NodeUtm* node1 = UtmNodeFind(&road->node_utm_structure, node1_id);
                NodeUtm* node2 = UtmNodeFind(&road->node_utm_structure, node2_id);

                RoadSegment road_segment_cur;
                RoadSegmentFromTwoRoadNodes(&road_segment_cur, node1, node2, road_width);

                RoadSegmentConnectionFromTwoRoadSegments(&road_segment_prev, &road_segment_cur,
                                                         road_width);

                // RoadIntersectionPointsFind(road, &road_segment_prev, way);
                QuadToBufferAdd(&road_segment_prev, vertex_buffer, index_buffer, way->id,
                                road->road_height, &current_vertex_idx, &current_index_idx);
                if (node_idx == way->node_count - 2)
                {
                    // RoadIntersectionPointsFind(road, &road_segment_cur, way);
                    QuadToBufferAdd(&road_segment_cur, vertex_buffer, index_buffer, way->id,
                                    road->road_height, &current_vertex_idx, &current_index_idx);
                }

                road_segment_prev = road_segment_cur;
                // TODO: what if road segment is consist of only one or two nodes
            }
        }
    }
    *out_vertex_buffer = vertex_buffer;
    *out_index_buffer = index_buffer;
}

static U64
HashU64FromStr8(String8 str)
{
    return HashU128FromStr8(str).u64[1];
}

static String8
Str8FromGCSCoordinates(Arena* arena, GCSBoundingBox* bbox)
{
    String8 str = {.str = (U8*)bbox, .size = sizeof(GCSBoundingBox)};
    String8 str_copy = PushStr8Copy(arena, str);
    return str_copy;
}

static B32
CacheNeedsUpdate(String8 cache_data_file, String8 cache_meta_file)
{
    ScratchScope scratch = ScratchScope(0, 0);
    U64 file_hash = 0;
    U64 file_timestamp = 0;
    B32 update_needed = false;

    U64 meta_file_size = 0;
    OS_Handle meta_data_file_handle = OS_FileOpen(OS_AccessFlag_Read, cache_meta_file);
    FileProperties meta_file_props = OS_PropertiesFromFile(meta_data_file_handle);
    meta_file_size = meta_file_props.size;
    if (meta_file_size == 0)
    {
        update_needed = true;
    }

    if (update_needed == false)
    {
        Rng1U64 meta_read_range = {0, meta_file_size};
        String8 meta_data_str = PushStr8FillByte(scratch.arena, meta_file_size, 0);
        U64 meta_file_bytes_read =
            OS_FileRead(meta_data_file_handle, meta_read_range, meta_data_str.str);

        if (meta_file_bytes_read != meta_file_size)
        {
            DEBUG_LOG("DataFetch: Could not read all data from meta cache file: %s",
                      cache_meta_file.str);
            update_needed = true;
        }
        U8* cur_byte = meta_data_str.str;
        if (update_needed == false)
        {
            U64 cur_file_hash = HashU64FromStr8(cache_data_file);

            //~mgj: read hash and ttl from file
            while (*cur_byte != 0 && *cur_byte != '\t')
            {
                cur_byte += 1;
            }
            Rng1U64 read_range = {0, U64(cur_byte - meta_data_str.str)};
            String8 u64_str = Str8Substr(meta_data_str, read_range);
            file_hash = U64FromStr8(u64_str, 10);
            if (cur_file_hash != file_hash)
            {
                update_needed = true;
            }

            if (update_needed == false)
            {
                while (*cur_byte == '\t')
                {
                    cur_byte += 1;
                }

                U64 cur_time = OS_NowUnix();
                U8* ttl_base = cur_byte;
                while (*cur_byte != 0)
                {
                    cur_byte += 1;
                }

                Rng1U64 ttl_range = {U64(ttl_base - meta_data_str.str),
                                     U64(cur_byte - meta_data_str.str)};
                file_timestamp = U64FromStr8(Str8Substr(meta_data_str, ttl_range), 10);
                if (cur_time > file_timestamp + 3600) // hard coded ttl of 1 hour
                {
                    update_needed = true;
                }
            }
        }
        OS_FileClose(meta_data_file_handle);
    }

    return update_needed;
}

static String8
DataFetch(Arena* arena, String8 cache_dir, String8 cache_file_name, String8 query,
          GCSBoundingBox* gcs_bbox)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;
    params.content_type = S("text/html");

    String8 query_str =
        PushStr8F(scratch.arena, (char*)query.str, gcs_bbox->lat_btm_left, gcs_bbox->lon_btm_left,
                  gcs_bbox->lat_top_right, gcs_bbox->lon_top_right);

    B32 read_from_cache = false;

    String8 cache_data_file = Str8PathFromStr8List(scratch.arena, {cache_dir, cache_file_name});
    String8 cache_meta_file = PushStr8Cat(scratch.arena, cache_data_file, S(".meta"));

    if (OS_FilePathExists(cache_data_file))
    {
        read_from_cache = true;
        if (OS_FilePathExists(cache_meta_file) == false)
        {
            read_from_cache = false;
        }
    }
    U8* str = 0;
    U64 size = 0;
    if (read_from_cache)
    {
        OS_Handle file_handle = OS_FileOpen(OS_AccessFlag_Read, cache_data_file);
        FileProperties file_props = OS_PropertiesFromFile(file_handle);

        str = PushArray(arena, U8, file_props.size);
        size = file_props.size;
        U64 total_read_size = OS_FileRead(file_handle, {.min = 0, .max = file_props.size}, str);

        if (total_read_size != file_props.size)
        {
            DEBUG_LOG("DataFetch: Could not read everything from cache");
        }
        OS_FileClose(file_handle);

        String8 input_str = Str8FromGCSCoordinates(scratch.arena, gcs_bbox);
        B32 needs_update = CacheNeedsUpdate(input_str, cache_meta_file);
        read_from_cache = !needs_update;
    }

    if (read_from_cache == false)
    {
        DEBUG_LOG("DataFetch: Fetching data from overpass-api.de\n");
        String8 host = S("https://overpass-api.de");
        String8 path = S("/api/interpreter");

        U32 max_retries = 3;
        B32 http_success = false;
        U32 retry_time_interval_ms = 2000;
        HTTP_Response response;
        do
        {
            response = HTTP_Request(arena, host, path, query_str, &params);

            if (response.good && response.code == HTTP_StatusCode_OK)
            {
                http_success = true;
                break;
            }

            DEBUG_LOG("DataFetch: Retrying...");
            Sleep(retry_time_interval_ms);
            max_retries -= 1;
        } while (max_retries >= 0);

        if (http_success == false)
        {
            ExitWithError("DataFetch: http request did not succeed after %d retries\n"
                          "Failed with error code: %d\n"
                          "Error message: %s\n",
                          max_retries, response.code, response.body.str);
        }

        str = response.body.str;
        size = response.body.size;

        // ~mgj: write to cache file
        OS_Handle file_write_handle = OS_FileOpen(OS_AccessFlag_Write, cache_data_file);
        U64 bytes_written = OS_FileWrite(file_write_handle, {.min = 0, .max = size}, str);
        if (bytes_written != size)
        {
            DEBUG_LOG("DataFetch: Was not able to write OSM data to cache\n");
        }
        OS_FileClose(file_write_handle);

        // ~mgj: write to cache meta file
        if (size > 0)
        {
            String8 input_str = Str8FromGCSCoordinates(scratch.arena, gcs_bbox);
            U64 new_hash = HashU64FromStr8(input_str);
            U64 timestamp = OS_NowUnix();
            String8 meta_str = PushStr8F(scratch.arena, "%llu\t%llu", new_hash, timestamp);

            OS_Handle file_write_handle_meta = OS_FileOpen(OS_AccessFlag_Write, cache_meta_file);
            U64 bytes_written_meta = OS_FileWrite(file_write_handle_meta,
                                                  {.min = 0, .max = meta_str.size}, meta_str.str);
            if (bytes_written_meta != meta_str.size)
            {
                DEBUG_LOG("DataFetch: Was not able to write meta data to cache\n");
            }
            OS_FileClose(file_write_handle_meta);
        }
    }

    String8 content = {str, size};
    return content;
}

static Vec3F32
HeightDimAdd(Vec2F32 pos, F32 height)
{
    return {pos.x, height, pos.y};
}

static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<Vertex3D> buffer, Buffer<U32> indices, U64 way_id,
                F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx)
{
    F32 road_width = Dist2F32(road_segment->start.top, road_segment->start.btm);
    F32 top_tex_scaled = Dist2F32(road_segment->start.top, road_segment->end.top) / road_width;
    F32 btm_tex_scaled = Dist2F32(road_segment->start.btm, road_segment->end.btm) / road_width;

    const F32 uv_x_top = 1;
    const F32 uv_x_btm = 0;
    const F32 uv_y_start = 0.5f - (F32)btm_tex_scaled;
    const F32 uv_y_end = 0.5f + (F32)top_tex_scaled;

    U32 base_vertex_idx = *cur_vertex_idx;
    U32 base_index_idx = *cur_index_idx;

    Vec2U32 id = {.u64 = way_id};

    // quad of vertices
    buffer.data[base_vertex_idx] = {.pos = HeightDimAdd(road_segment->start.top, road_height),
                                    .uv = {uv_x_top, uv_y_start},
                                    .object_id = id};
    buffer.data[base_vertex_idx + 1] = {.pos = HeightDimAdd(road_segment->start.btm, road_height),
                                        .uv = {uv_x_btm, uv_y_start},
                                        .object_id = id};
    buffer.data[base_vertex_idx + 2] = {.pos = HeightDimAdd(road_segment->end.top, road_height),
                                        .uv = {uv_x_top, uv_y_end},
                                        .object_id = id};
    buffer.data[base_vertex_idx + 3] = {.pos = HeightDimAdd(road_segment->end.btm, road_height),
                                        .uv = {uv_x_btm, uv_y_end},
                                        .object_id = id};

    // creating quad from
    indices.data[base_index_idx] = base_vertex_idx;
    indices.data[base_index_idx + 1] = base_vertex_idx + 1;
    indices.data[base_index_idx + 2] = base_vertex_idx + 2;
    indices.data[base_index_idx + 3] = base_vertex_idx + 1;
    indices.data[base_index_idx + 4] = base_vertex_idx + 2;
    indices.data[base_index_idx + 5] = base_vertex_idx + 3;

    *cur_vertex_idx += 4;
    *cur_index_idx += 6;
}

static NodeUtm*
NodeUtmFind(NodeUtmStructure* node_ways, U64 node_id)
{
    U64 node_index = node_id % node_ways->node_hashmap.size;
    NodeUtmSlot* slot = &node_ways->node_hashmap.data[node_index];
    NodeUtm* node = slot->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
            return node;
    }
    return &g_road_node_utm;
}

static void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, Way* current_road_way)
{
    ScratchScope scratch = ScratchScope(0, 0);
    // for each end of segment find whether it there is a road crossing. If there is, change the
    // location of the segment based on roads that intersect with it
    const U32 number_of_cross_sections = 2;
    RoadCrossSection* road_cross_sections[number_of_cross_sections] = {&in_out_segment->start,
                                                                       &in_out_segment->end};

    for (U32 i = 0; i < number_of_cross_sections; i++)
    {
        RoadCrossSection* road_cross_section = road_cross_sections[i];
        NodeUtm* node = road_cross_section->node;
        RoadCrossSection* opposite_cross_section =
            road_cross_sections[(i + 1) % number_of_cross_sections];

        Rng2F32 top_of_road_line_segment = {
            opposite_cross_section->top.x,
            opposite_cross_section->top.y,
            road_cross_section->top.x,
            road_cross_section->top.y,
        };
        Rng2F32 btm_of_road_line_segment = {
            opposite_cross_section->btm.x,
            opposite_cross_section->btm.y,
            road_cross_section->btm.x,
            road_cross_section->btm.y,
        };

        // iterate crossing roads
        F32 shortest_distance_top =
            Dist2F32(top_of_road_line_segment.p0, top_of_road_line_segment.p1);
        Vec2F32 shortest_distance_pt_top = road_cross_section->top;
        F32 shortest_distance_btm =
            Dist2F32(btm_of_road_line_segment.p0, btm_of_road_line_segment.p1);
        Vec2F32 shortest_distance_pt_btm = road_cross_section->btm;

        for (WayNode* road_way_list = node->way_queue.first; road_way_list;
             road_way_list = road_way_list->next)
        {
            Way* way = road_way_list->road_way;
            F32 road_width =
                TagValueF32Get(scratch.arena, S("width"), road->default_road_width, way->tags);
            if (way->id != current_road_way->id)
            {
                // find adjacent nodes on crossing road
                AdjacentNodeLL* adj_node_ll = {};
                for (U32 node_idx = 0; node_idx < way->node_count; node_idx++)
                {
                    U64 node_id = way->node_ids[node_idx];
                    if (node_id == node->id)
                    {
                        if (node_idx > 0)
                        {
                            U32 prev_node_idx = node_idx - 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 prev_node_id = way->node_ids[prev_node_idx];
                            adj_node->node = NodeUtmFind(&road->node_utm_structure, prev_node_id);
                            SLLStackPush(adj_node_ll, adj_node);
                        }

                        if (node_idx < way->node_count - 1)
                        {
                            U32 next_node_idx = node_idx + 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 next_node_id = way->node_ids[next_node_idx];
                            adj_node->node = NodeUtmFind(&road->node_utm_structure, next_node_id);
                            SLLStackPush(adj_node_ll, adj_node);
                        }
                        break;
                    }
                }

                //~ mgj: Create road segments from adjacent nodes and find the intersection
                // points
                // with current road. We need to find the intersection between both sides of the
                // current
                // road and where it intersects both sides of the crossing road
                for (AdjacentNodeLL* adj_node_item = adj_node_ll; adj_node_item;
                     adj_node_item = adj_node_item->next)
                {
                    Vec2F64 intersection_pt;
                    RoadSegment crossing_road_segment = {};
                    RoadSegmentFromTwoRoadNodes(&crossing_road_segment, adj_node_item->node, node,
                                                road_width);
                    // update closest point for the top road
                    if (ui::LineIntersect(
                            top_of_road_line_segment.p0.x, top_of_road_line_segment.p0.y,
                            top_of_road_line_segment.p1.x, top_of_road_line_segment.p1.y,
                            crossing_road_segment.start.top.x, crossing_road_segment.start.top.y,
                            crossing_road_segment.end.top.x, crossing_road_segment.end.top.y,
                            &intersection_pt.x, &intersection_pt.y))
                    {
                        Vec2F32 intersection_pt_f32 = {(F32)intersection_pt.x,
                                                       (F32)intersection_pt.y};
                        F32 dist = Dist2F32(top_of_road_line_segment.p0, intersection_pt_f32);
                        if (dist < shortest_distance_top)
                        {
                            shortest_distance_top = dist;
                            shortest_distance_pt_top = intersection_pt_f32;
                        }
                    };

                    if (ui::LineIntersect(
                            top_of_road_line_segment.p0.x, top_of_road_line_segment.p0.y,
                            top_of_road_line_segment.p1.x, top_of_road_line_segment.p1.y,
                            crossing_road_segment.start.btm.x, crossing_road_segment.start.btm.y,
                            crossing_road_segment.end.btm.x, crossing_road_segment.end.btm.y,
                            &intersection_pt.x, &intersection_pt.y))
                    {
                        Vec2F32 intersection_pt_f32 = {(F32)intersection_pt.x,
                                                       (F32)intersection_pt.y};
                        F32 dist = Dist2F32(top_of_road_line_segment.p0, intersection_pt_f32);
                        if (dist < shortest_distance_top)
                        {
                            shortest_distance_top = dist;
                            shortest_distance_pt_top = intersection_pt_f32;
                        }
                    };
                    // update closest point for the btm
                    if (ui::LineIntersect(
                            btm_of_road_line_segment.p0.x, btm_of_road_line_segment.p0.y,
                            btm_of_road_line_segment.p1.x, btm_of_road_line_segment.p1.y,
                            crossing_road_segment.start.top.x, crossing_road_segment.start.top.y,
                            crossing_road_segment.end.top.x, crossing_road_segment.end.top.y,
                            &intersection_pt.x, &intersection_pt.y))
                    {
                        Vec2F32 intersection_pt_f32 = {(F32)intersection_pt.x,
                                                       (F32)intersection_pt.y};
                        F32 dist = Dist2F32(btm_of_road_line_segment.p0, intersection_pt_f32);
                        if (dist < shortest_distance_btm)
                        {
                            shortest_distance_btm = dist;
                            shortest_distance_pt_btm = intersection_pt_f32;
                        }
                    };

                    if (ui::LineIntersect(
                            btm_of_road_line_segment.p0.x, btm_of_road_line_segment.p0.y,
                            btm_of_road_line_segment.p1.x, btm_of_road_line_segment.p1.y,
                            crossing_road_segment.start.btm.x, crossing_road_segment.start.btm.y,
                            crossing_road_segment.end.btm.x, crossing_road_segment.end.btm.y,
                            &intersection_pt.x, &intersection_pt.y))
                    {
                        Vec2F32 intersection_pt_f32 = {(F32)intersection_pt.x,
                                                       (F32)intersection_pt.y};
                        F32 dist = Dist2F32(btm_of_road_line_segment.p0, intersection_pt_f32);
                        if (dist < shortest_distance_btm)
                        {
                            shortest_distance_btm = dist;
                            shortest_distance_pt_btm = intersection_pt_f32;
                        }
                    };
                }
            }
        }

        road_cross_section->btm = shortest_distance_pt_btm;
        road_cross_section->top = shortest_distance_pt_top;
    }
}

static NodeUtm*
RandomUtmNodeFind(NodeUtmStructure* utm_node_structure)
{
    U32 rand_num = RandomU32();
    for (U32 i = 0; i < utm_node_structure->node_hashmap.size; ++i)
    {
        U32 slot_index = rand_num % utm_node_structure->node_hashmap.size;
        NodeUtmSlot* slot = &utm_node_structure->node_hashmap.data[slot_index];
        if (slot->first != NULL)
        {
            return slot->first;
        }
        rand_num++;
    }
    return &g_road_node_utm;
}

static NodeUtm*
UtmNodeFind(NodeUtmStructure* utm_node_structure, U64 node_id)
{
    U32 slot_index = node_id % utm_node_structure->node_hashmap.size;
    NodeUtmSlot* slot = &utm_node_structure->node_hashmap.data[slot_index];
    for (NodeUtm* node = slot->first; node; node = node->next)
    {
        if (node->id == node_id)
        {
            return node;
        }
    }
    return &g_road_node_utm;
}

static NodeUtm*
NeighbourNodeChoose(NodeUtm* node, Road* road)
{
    // Calculate roadway count for the node
    U32 roadway_count = 0;
    for (WayNode* way_element = node->way_queue.first; way_element; way_element = way_element->next)
    {
        roadway_count++;
    }
    WayNode* way_element = node->way_queue.first;

    // Find random roadway
    U32 rand_num = RandomU32();
    U32 rand_roadway_idx = rand_num % roadway_count;
    for (U32 i = 0; i < rand_roadway_idx; ++i)
    {
        way_element = way_element->next;
    }
    Assert(way_element);

    Way* way = way_element->road_way;
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
    NodeUtm* next_node = UtmNodeFind(&road->node_utm_structure, next_node_id);
    return next_node;
}

// ~mgj: Cars

static Rng1F32
CarCenterHeightOffset(Buffer<Vertex3D> vertices)
{
    F32 highest_value = 0;
    for (int i = 0; i < vertices.size; i++)
    {
        highest_value = Max(highest_value, vertices.data[i].pos.z);
    }

    F32 lowest_value = highest_value;
    for (int i = 0; i < vertices.size; i++)
    {
        lowest_value = Min(lowest_value, vertices.data[i].pos.z);
    }

    return {.min = lowest_value, .max = highest_value};
}

static CarSim*
CarSimCreate(String8 asset_path, String8 texture_path, U32 car_count, Road* road)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();

    CarSim* car_sim = PushStruct(arena, CarSim);
    car_sim->arena = arena;

    // parse gltf file
    String8 gltf_path = Str8PathFromStr8List(scratch.arena, {asset_path, S("cars/scene.gltf")});
    wrapper::CgltfResult parsed_result = wrapper::CgltfParse(arena, gltf_path, S("Car.013"));
    R_SamplerInfo sampler_info = SamplerFromCgltfSampler(parsed_result.sampler);
    car_sim->vertex_buffer = parsed_result.vertex_buffer;
    car_sim->index_buffer = parsed_result.index_buffer;
    car_sim->sampler_info = sampler_info;

    R_BufferInfo vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(car_sim->vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo index_buffer_info =
        R_BufferInfoFromTemplateBuffer(car_sim->index_buffer, R_BufferType_Index);

    car_sim->texture_path = Str8PathFromStr8List(arena, {texture_path, S("car_collection.ktx2")});
    car_sim->texture_handle =
        R_TextureLoad(&sampler_info, car_sim->texture_path, R_PipelineUsageType_3DInstanced);
    car_sim->vertex_handle = R_BufferLoad(&vertex_buffer_info);
    car_sim->index_handle = R_BufferLoad(&index_buffer_info);

    car_sim->car_center_offset = CarCenterHeightOffset(car_sim->vertex_buffer);
    car_sim->cars = BufferAlloc<Car>(arena, car_count);

    for (U32 i = 0; i < car_count; ++i)
    {
        NodeUtm* source_node = RandomUtmNodeFind(&road->node_utm_structure);
        NodeUtm* target_node = NeighbourNodeChoose(source_node, road);
        city::Car* car = &car_sim->cars.data[i];
        car->source = source_node;
        car->target = target_node;
        car->speed = 10.0f;
        car->cur_pos =
            glm::vec3(source_node->pos.x, road->road_height - car_sim->car_center_offset.min,
                      source_node->pos.y);
        car->dir = glm::normalize(glm::vec3(target_node->pos.x - source_node->pos.x, 0,
                                            target_node->pos.y - source_node->pos.y));
    }

    return car_sim;
}

static void
CarSimDestroy(CarSim* car_sim)
{
    VK_AssetManagerBufferFree(car_sim->vertex_handle);
    VK_AssetManagerBufferFree(car_sim->index_handle);
    VK_AssetManagerTextureFree(car_sim->texture_handle);
    ArenaRelease(car_sim->arena);
}

static Buffer<Model3DInstance>
CarUpdate(Arena* arena, CarSim* car, Road* road, F32 time_delta)
{
    Buffer<Model3DInstance> instance_buffer = BufferAlloc<Model3DInstance>(arena, car->cars.size);
    Model3DInstance* instance;
    city::Car* car_info;

    F32 car_speed_default = 5.0f; // m/s
    for (U32 car_idx = 0; car_idx < car->cars.size; car_idx++)
    {
        instance = &instance_buffer.data[car_idx];
        car_info = &car->cars.data[car_idx];

        glm::vec3 target_pos =
            glm::vec3(car_info->target->pos.x, car_info->cur_pos.y, car_info->target->pos.y);
        glm::vec3 new_pos = car_info->cur_pos + car_info->dir * car_speed_default * time_delta;

        // Is the destination point in between new and old pos?
        F32 min_x = Min(car_info->cur_pos.x, new_pos.x);
        F32 max_x = Max(car_info->cur_pos.x, new_pos.x);
        F32 min_y = Min(car_info->cur_pos.y, new_pos.y);
        F32 max_y = Max(car_info->cur_pos.y, new_pos.y);

        // Check if the car has reached its destination. If so, find new destination and
        // direction.
        if ((target_pos.x >= min_x && target_pos.x <= max_x) &&
            (target_pos.y >= min_y && target_pos.y <= max_y))
        {
            NodeUtm* new_target = NeighbourNodeChoose(car_info->target, road);
            glm::vec3 new_target_pos =
                glm::vec3(new_target->pos.x, car_info->cur_pos.y, new_target->pos.y);
            glm::vec3 new_dir = glm::normalize(new_target_pos - new_pos);
            car_info->dir = new_dir;
            car_info->source = car_info->target;
            car_info->target = new_target;
        }

        glm::vec3 y_basis = car_info->dir;
        y_basis *= -1; // In model space, the front of the car points in the negative y direction
        glm::vec3 x_basis = glm::vec3(-y_basis.z, 0, y_basis.x);
        glm::vec3 z_basis = glm::cross(x_basis, y_basis);

        instance->x_basis = glm::vec4(x_basis, 0);
        instance->y_basis = glm::vec4(y_basis, 0);
        instance->z_basis = glm::vec4(z_basis, 0);
        instance->w_basis = {new_pos, 1};

        car_info->cur_pos = new_pos;
    }
    return instance_buffer;
}
// ~mgj: Buildings

static Buildings*
BuildingsCreate(String8 cache_path, String8 texture_path, F32 road_height, GCSBoundingBox* gcs_bbox,
                R_SamplerInfo* sampler_info)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();
    Buildings* buildings = PushStruct(arena, Buildings);
    buildings->arena = arena;
    buildings->cache_file_name = PushStr8Copy(arena, S("openapi_node_ways_buildings.json"));

    String8 query = S(R"(data=
        [out:json] [timeout:25];
        (
          way["building"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )");
    String8 data =
        DataFetch(scratch.arena, cache_path, buildings->cache_file_name, query, gcs_bbox);
    wrapper::OverpassNodeWayParse(arena, data, 100, &buildings->node_ways);
    U64 hashmap_slot_count = 100;
    NodeStructureCreate(arena, &buildings->node_ways, gcs_bbox, hashmap_slot_count,
                        &buildings->node_utm_structure);

    buildings->facade_texture_path =
        Str8PathFromStr8List(arena, {texture_path, S("brick_wall.ktx2")});
    buildings->roof_texture_path =
        Str8PathFromStr8List(arena, {texture_path, S("concrete042A.ktx2")});
    buildings->facade_texture_handle =
        R_TextureLoad(sampler_info, buildings->facade_texture_path, R_PipelineUsageType_3D);
    buildings->roof_texture_handle =
        R_TextureLoad(sampler_info, buildings->roof_texture_path, R_PipelineUsageType_3D);

    BuildingRenderInfo render_info;
    city::BuildingsBuffersCreate(arena, buildings, road_height, &render_info);
    R_BufferInfo vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(render_info.vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo index_buffer_info =
        R_BufferInfoFromTemplateBuffer(render_info.index_buffer, R_BufferType_Index);

    buildings->vertex_handle = R_BufferLoad(&vertex_buffer_info);
    buildings->index_handle = R_BufferLoad(&index_buffer_info);
    buildings->roof_index_buffer_offset = render_info.roof_index_offset;
    buildings->facade_index_buffer_offset = render_info.facade_index_offset;
    buildings->facade_index_count = render_info.facade_index_count;
    buildings->roof_index_count = render_info.roof_index_count;

    return buildings;
}

static void
BuildingDestroy(Buildings* building)
{
    VK_AssetManagerBufferFree(building->vertex_handle);
    VK_AssetManagerBufferFree(building->index_handle);
    VK_AssetManagerTextureFree(building->roof_texture_handle);
    VK_AssetManagerTextureFree(building->facade_texture_handle);
    ArenaRelease(building->arena);
}
static F32
Cross2F32ZComponent(Vec2F32 a, Vec2F32 b)
{
    return a.x * b.y - a.y * b.x;
}
static B32
AreTwoConnectedLineSegmentsCollinear(Vec2F32 prev, Vec2F32 cur, Vec2F32 next)
{
    Vec2F32 ba = Sub2F32(prev, cur);
    Vec2F32 ac = Sub2F32(next, prev);

    F32 cross_product_z = Cross2F32ZComponent(ba, ac);
    B32 is_collinear = FALSE;
    if (cross_product_z == 0)
    {
        is_collinear = TRUE;
    }
    return is_collinear;
}
// TODO: Built the roof, which requires a way to divide the concave polygons (that are the
// buildings) and divide it into convex parts
static void
BuildingsBuffersCreate(Arena* arena, Buildings* buildings, F32 road_height,
                       BuildingRenderInfo* out_render_info)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    NodeWays* node_ways = &buildings->node_ways;
    NodeUtmStructure* node_utm_structure = &buildings->node_utm_structure;
    F32 building_height = 3;

    // ~mgj: Calculate vertex buffer size based on node count
    U32 total_vertex_count = 0;
    U32 total_index_count = 0;

    for (U32 i = 0; i < node_ways->ways.size; i++)
    {
        Way* way = &node_ways->ways.data[i];
        // ~mgj: first and last node id should be the same
        U32 way_facade_vertex_count = (way->node_count - 1) * 4;
        total_vertex_count += way_facade_vertex_count + (way->node_count - 1) * 2;
        // ~mgj: count of index for Polyhedron (without ground floor) that makes up the building
        U64 sides_triangle_count = (way->node_count - 1) * 2;
        U64 roof_triangle_count = way->node_count - 2;
        U64 total_triangle_count = sides_triangle_count + roof_triangle_count;
        total_index_count += total_triangle_count * 3;

        Assert(way->node_ids[0] == way->node_ids[way->node_count - 1]);
    }

    Buffer<Vertex3D> vertex_buffer = BufferAlloc<Vertex3D>(scratch.arena, total_vertex_count);
    Buffer<U32> index_buffer = BufferAlloc<U32>(scratch.arena, total_index_count);

    U32 base_index_idx = 0;
    U32 base_vertex_idx = 0;
    for (U32 way_idx = 0; way_idx < node_ways->ways.size; way_idx++)
    {
        Way* way = &node_ways->ways.data[way_idx];

        // ~mgj: Add Vertices and Indices for the sides of building
        for (U32 node_idx = 0, vert_idx = base_vertex_idx, index_idx = base_index_idx;
             node_idx < way->node_count - 1; node_idx++, vert_idx += 4, index_idx += 6)
        {
            NodeUtm* utm_node = NodeUtmFind(node_utm_structure, way->node_ids[node_idx]);
            NodeUtm* utm_node_next = NodeUtmFind(node_utm_structure, way->node_ids[node_idx + 1]);
            F32 side_width = Length2F32(Sub2F32(utm_node->pos, utm_node_next->pos));

            Vec2U32 id = {.u64 = way->id};

            vertex_buffer.data[vert_idx] = {.pos = {utm_node->pos.x, road_height, utm_node->pos.y},
                                            .uv = {0.0f, 0.0f},
                                            .object_id = id};
            vertex_buffer.data[vert_idx + 1] = {
                .pos = {utm_node->pos.x, road_height + building_height, utm_node->pos.y},
                .uv = {0.0f, building_height},
                .object_id = id};

            vertex_buffer.data[vert_idx + 2] = {
                .pos = {utm_node_next->pos.x, road_height, utm_node_next->pos.y},
                .uv = {side_width, 0.0f},
                .object_id = id};
            vertex_buffer.data[vert_idx + 3] = {
                .pos = {utm_node_next->pos.x, road_height + building_height, utm_node_next->pos.y},
                .uv = {side_width, building_height},
                .object_id = id};

            index_buffer.data[index_idx] = vert_idx;
            index_buffer.data[index_idx + 1] = vert_idx + 1;
            index_buffer.data[index_idx + 2] = vert_idx + 2;
            index_buffer.data[index_idx + 3] = vert_idx + 1;
            index_buffer.data[index_idx + 4] = vert_idx + 2;
            index_buffer.data[index_idx + 5] = vert_idx + 3;
        }

        base_index_idx += (way->node_count - 1) * 6;
        base_vertex_idx += (way->node_count - 1) * 4;
    }

    ///////////////////////////////////////////////////////////////////
    // ~mgj: Create roof
    U32 roof_base_index = base_index_idx;
    for (U32 way_idx = 0; way_idx < node_ways->ways.size; way_idx++)
    {
        Way* way = &node_ways->ways.data[way_idx];
        Buffer<NodeUtm*> buildings_utm_node_buffer =
            BufferAlloc<NodeUtm*>(scratch.arena, way->node_count - 1);
        for (U32 idx = 0; idx < way->node_count - 1; idx += 1)
        {
            buildings_utm_node_buffer.data[idx] =
                NodeUtmFind(node_utm_structure, way->node_ids[idx]);
        }

        // ~mgj: ignore collinear line segments
        Buffer<NodeUtm*> final_utm_node_buffer =
            BufferAlloc<NodeUtm*>(scratch.arena, buildings_utm_node_buffer.size);
        {
            U32 cur_idx = 0;
            for (U32 idx = 0; idx < buildings_utm_node_buffer.size; idx += 1)
            {
                Vec2F32 prev_pos = buildings_utm_node_buffer
                                       .data[(buildings_utm_node_buffer.size + idx - 1) %
                                             buildings_utm_node_buffer.size]
                                       ->pos;
                Vec2F32 cur_pos =
                    buildings_utm_node_buffer.data[idx % buildings_utm_node_buffer.size]->pos;
                Vec2F32 next_pos =
                    buildings_utm_node_buffer.data[(idx + 1) % buildings_utm_node_buffer.size]->pos;

                B32 is_collinear =
                    AreTwoConnectedLineSegmentsCollinear(prev_pos, cur_pos, next_pos);
                if (!is_collinear)
                {
                    final_utm_node_buffer.data[cur_idx++] = buildings_utm_node_buffer.data[idx];
                }
            }
            final_utm_node_buffer.size = cur_idx;
        }

        // ~mgj: prepare for ear clipping algo
        Buffer<Vec2F32> node_pos_buffer =
            BufferAlloc<Vec2F32>(scratch.arena, final_utm_node_buffer.size);
        for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
        {
            NodeUtm* node_utm = final_utm_node_buffer.data[idx];
            node_pos_buffer.data[idx] = node_utm->pos;
        }

        Buffer<U32> polygon_index_buffer = EarClipping(scratch.arena, node_pos_buffer);
        if (polygon_index_buffer.size > 0)
        {
            // vertex buffer fill
            for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
            {
                NodeUtm* node_utm = final_utm_node_buffer.data[idx];
                Vec2U32 id = {.u64 = node_utm->id};
                vertex_buffer.data[base_vertex_idx + idx] = {
                    .pos = {node_utm->pos.x, road_height + building_height, node_utm->pos.y},
                    .uv = {node_utm->pos.x, node_utm->pos.y},
                    .object_id = id};
            }

            // index buffer fill
            for (U32 idx = 0; idx < polygon_index_buffer.size; idx += 1)
            {
                index_buffer.data[base_index_idx + idx] =
                    polygon_index_buffer.data[idx] + base_vertex_idx;
            }

            base_vertex_idx += final_utm_node_buffer.size;
            base_index_idx += polygon_index_buffer.size;
        }
    }

    Buffer<Vertex3D> vertex_buffer_final = BufferAlloc<Vertex3D>(arena, base_vertex_idx);
    Buffer<U32> index_buffer_final = BufferAlloc<U32>(arena, base_index_idx);
    BufferCopy(vertex_buffer_final, vertex_buffer, base_vertex_idx);
    BufferCopy(index_buffer_final, index_buffer, base_index_idx);

    out_render_info->vertex_buffer = vertex_buffer_final;
    out_render_info->index_buffer = index_buffer_final;
    out_render_info->facade_index_offset = 0;
    out_render_info->roof_index_offset = roof_base_index;
    out_render_info->facade_index_count = roof_base_index;
    out_render_info->roof_index_count = base_index_idx - roof_base_index;
}

enum Direction
{
    Direction_Undefined,
    Direction_Clockwise,
    Direction_CounterClockwise
};

static Direction
ClockWiseTest(Buffer<Vec2F32> node_buffer)
{
    F32 total = 0;
    for (U32 idx = 0; idx < node_buffer.size; idx += 1)
    {
        Vec2F32 a = node_buffer.data[idx];
        Vec2F32 b = node_buffer.data[(idx + 1) % node_buffer.size];

        F32 cross_product_z = Cross2F32ZComponent(a, b);
        total += cross_product_z;
    }
    if (total > 0)
    {
        return Direction_CounterClockwise;
    }
    else if (total < 0)
    {
        return Direction_Clockwise;
    }
    DEBUG_LOG("ClockWiseTest: Lines are collinear\n");
    return Direction_Undefined;
}

static Buffer<U32>
IndexBufferCreate(Arena* arena, U64 buffer_size, Direction direction)
{
    Buffer<U32> index_buffer = BufferAlloc<U32>(arena, buffer_size);
    if (direction == Direction_Clockwise)
    {
        for (U32 i = 0; i < index_buffer.size; i++)
        {
            index_buffer.data[i] = i;
        }
    }
    else if (direction == Direction_CounterClockwise)
    {
        for (U32 i = 0; i < index_buffer.size; i++)
        {
            index_buffer.data[i] = index_buffer.size - i - 1;
        }
    }
    else if (direction == Direction_Undefined)
    {
        Assert(0);
    }
    return index_buffer;
}

// Shoelace Algorithm
// source: https://artofproblemsolving.com/wiki/index.php/Shoelace_Theorem
static B32
PointInTriangle(Vec2F32 p1, Vec2F32 p2, Vec2F32 p3, Vec2F32 point)
{
    F32 d1, d2, d3;
    B32 has_neg, has_pos;

    d1 = Cross2F32ZComponent(Sub2F32(point, p1), Sub2F32(p2, p1));
    d2 = Cross2F32ZComponent(Sub2F32(point, p2), Sub2F32(p3, p2));
    d3 = Cross2F32ZComponent(Sub2F32(point, p3), Sub2F32(p1, p3));

    has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

static void
NodeBufferPrintDebug(Buffer<Vec2F32> node_buffer)
{
    DEBUG_LOG("Error in ear clipping algo. Expecting vertex_count-2 number of triangles\n"
              "The following vertices are the problem: \n");
    for (U32 pt_idx = 0; pt_idx < node_buffer.size; pt_idx++)
    {
        printf("%d, %f, %f\n", pt_idx, node_buffer.data[pt_idx].x, node_buffer.data[pt_idx].y);
    }
}

static Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F32> node_buffer)
{
    Assert(node_buffer.size >= 3);
    ScratchScope scratch = ScratchScope(&arena, 1);

    U32 total_triangle_count = (node_buffer.size - 2);
    U32 total_index_count = total_triangle_count * 3;

    Direction direction = ClockWiseTest(node_buffer);
    if (direction == Direction_Undefined)
    {
        DEBUG_LOG("Cannot determine direction\n");
        DEBUG_FUNC(NodeBufferPrintDebug(node_buffer));
        return {0, 0};
    }
    Buffer<U32> index_buffer = IndexBufferCreate(scratch.arena, node_buffer.size, direction);
    Buffer<U32> out_vertex_index_buffer = BufferAlloc<U32>(arena, total_index_count);
    U32 cur_index_buffer_idx = 0;
    U32 idx = 0;
    for (; idx < index_buffer.size; idx++)
    {
        if (index_buffer.size < 3)
        {
            break;
        }

        U32 ear_index_buffer_idx = idx % index_buffer.size;
        U32 prev_index_buffer_idx = (index_buffer.size + idx - 1) % index_buffer.size;
        U32 next_index_buffer_idx = (index_buffer.size + idx + 1) % index_buffer.size;

        U32 ear_node_buffer_idx = index_buffer.data[ear_index_buffer_idx];
        U32 prev_node_buffer_idx = index_buffer.data[prev_index_buffer_idx];
        U32 next_node_buffer_idx = index_buffer.data[next_index_buffer_idx];

        Vec2F32 ear = node_buffer.data[ear_node_buffer_idx];
        Vec2F32 prev = node_buffer.data[prev_node_buffer_idx];
        Vec2F32 next = node_buffer.data[next_node_buffer_idx];

        Vec2F32 prev_to_ear = Sub2F32(ear, prev);
        Vec2F32 ear_to_next = Sub2F32(next, ear);

        F32 cross_product_z = Cross2F32ZComponent(prev_to_ear, ear_to_next);

        // negative cross product z component means that the triangle has clockwise orientation.
        if (cross_product_z < 0)
        {
            B32 is_ear = TRUE;
            for (U32 test_i = 0; test_i < index_buffer.size - 3; test_i++)
            {
                U32 test_node_buffer_idx =
                    index_buffer.data[(next_index_buffer_idx + test_i + 1) % index_buffer.size];
                Vec2F32 test_point = node_buffer.data[test_node_buffer_idx];

                if (PointInTriangle(prev, ear, next, test_point))
                {
                    is_ear = FALSE;
                    break;
                }
            }

            if (is_ear)
            {
                // add ear to vertex buffer
                out_vertex_index_buffer.data[cur_index_buffer_idx] = prev_node_buffer_idx;
                out_vertex_index_buffer.data[cur_index_buffer_idx + 1] = ear_node_buffer_idx;
                out_vertex_index_buffer.data[cur_index_buffer_idx + 2] = next_node_buffer_idx;
                cur_index_buffer_idx += 3;

                // remove ear from index buffer
                BufferItemRemove(&index_buffer, ear_index_buffer_idx);
                idx = 0;
            }
        }
        else if (cross_product_z == 0)
        {
            DEBUG_LOG("Error in EarClipping: two line segments are collinear\n");
        }
    }
    if (cur_index_buffer_idx != out_vertex_index_buffer.size)
    {
        DEBUG_FUNC(NodeBufferPrintDebug(node_buffer));
        out_vertex_index_buffer.size = cur_index_buffer_idx;
    }

    Assert(cur_index_buffer_idx == out_vertex_index_buffer.size);
    return out_vertex_index_buffer;
}

// ~mgj: Bounding Box is defined as the bottom left corner to the top right corner
static Rng2F32
UtmFromBoundingBox(GCSBoundingBox bbox)
{
    F64 long_low_utm;
    F64 lat_low_utm;
    F64 long_high_utm;
    F64 lat_high_utm;
    char utm_zone[10];
    UTM::LLtoUTM(bbox.lat_btm_left, bbox.lon_btm_left, lat_low_utm, long_low_utm, utm_zone);
    UTM::LLtoUTM(bbox.lat_top_right, bbox.lon_top_right, lat_high_utm, long_high_utm, utm_zone);

    Rng2F32 utm_bb = {(F32)lat_low_utm, (F32)long_low_utm, (F32)lat_high_utm, (F32)long_high_utm};
    return utm_bb;
}

} // namespace city
