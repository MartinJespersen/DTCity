namespace city
{

g_internal void
road_destroy(Road* road)
{
    r_buffer_destroy(road->handles.vertex_buffer_handle);
    r_buffer_destroy(road->handles.index_buffer_handle);
    r_texture_destroy(road->handles.texture_handle);

    ArenaRelease(road->arena);
}

g_internal void
RoadSegmentFromTwoRoadNodes(RoadSegment* out_road_segment, osm::UtmNode* node_0,
                            osm::UtmNode* node_1, F32 road_width)
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
g_internal void
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

g_internal F32
tag_value_get(Arena* arena, String8 key, F32 default_width, Buffer<osm::Tag> tags)
{
    F32 road_width = default_width; // Example value, adjust as needed
    {
        osm::TagResult result = osm::tag_find(arena, tags, key);
        if (result.result == osm::TagResultEnum::ROAD_TAG_FOUND)
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

g_internal city::RenderBuffers
road_render_buffers_create(Arena* arena, Buffer<city::RoadEdge> edge_buffer, F32 default_road_width,
                           F32 road_height)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Buffer<r_Vertex3D> vertex_buffer = BufferAlloc<r_Vertex3D>(arena, edge_buffer.size * 4);
    Buffer<U32> index_buffer = BufferAlloc<U32>(arena, edge_buffer.size * 6);

    U32 cur_vertex_idx = 0;
    U32 cur_index_idx = 0;
    for (auto& edge : edge_buffer)
    {
        osm::UtmNode* start_node = osm::utm_node_find(edge.node_id_from);
        osm::UtmNode* end_node = osm::utm_node_find(edge.node_id_to);
        osm::WayNode* way_node = osm::way_find(edge.way_id);
        osm::Way* way = &way_node->way;

        F32 road_width = tag_value_get(scratch.arena, S("width"), default_road_width, way->tags);

        RoadSegment road_segment;
        RoadSegmentFromTwoRoadNodes(&road_segment, start_node, end_node, default_road_width);

        RoadEdge* prev_edge = edge.prev;
        if (prev_edge)
        {
            osm::UtmNode* start_node_prev = osm::utm_node_find(prev_edge->node_id_from);
            osm::UtmNode* end_node_prev = osm::utm_node_find(prev_edge->node_id_to);
            RoadSegment road_segment_prev;
            RoadSegmentFromTwoRoadNodes(&road_segment_prev, start_node_prev, end_node_prev,
                                        road_width);
            RoadSegmentConnectionFromTwoRoadSegments(&road_segment_prev, &road_segment, road_width);
        }

        RoadEdge* next_edge = edge.next;
        if (next_edge)
        {
            osm::UtmNode* start_node_next = osm::utm_node_find(next_edge->node_id_from);
            osm::UtmNode* end_node_next = osm::utm_node_find(next_edge->node_id_to);
            RoadSegment road_segment_next;
            RoadSegmentFromTwoRoadNodes(&road_segment_next, start_node_next, end_node_next,
                                        road_width);
            RoadSegmentConnectionFromTwoRoadSegments(&road_segment, &road_segment_next, road_width);
        }

        QuadToBufferAdd(&road_segment, vertex_buffer, index_buffer, edge.id, road_height,
                        &cur_vertex_idx, &cur_index_idx);
    }

    city::RenderBuffers render_buffers = {.vertices = vertex_buffer, .indices = index_buffer};
    return render_buffers;
}

g_internal U64
HashU64FromStr8(String8 str)
{
    return hash_u128_from_str8(str).u64[1];
}

g_internal B32
cache_needs_update(String8 cache_data_file, String8 cache_meta_file)
{
    ScratchScope scratch = ScratchScope(0, 0);
    U64 file_hash = 0;
    U64 file_timestamp = 0;
    B32 update_needed = false;

    U64 meta_file_size = 0;
    OS_Handle meta_data_file_handle = os_file_open(OS_AccessFlag_Read, cache_meta_file);
    FileProperties meta_file_props = os_properties_from_file(meta_data_file_handle);
    meta_file_size = meta_file_props.size;
    if (meta_file_size == 0)
    {
        update_needed = true;
    }

    if (update_needed == false)
    {
        Rng1U64 meta_read_range = {0, meta_file_size};
        String8 meta_data_str = push_str8_fill_byte(scratch.arena, meta_file_size, 0);
        U64 meta_file_bytes_read =
            os_file_read(meta_data_file_handle, meta_read_range, meta_data_str.str);

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

                U64 cur_time = os_now_unix();
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
        os_file_close(meta_data_file_handle);
    }

    return update_needed;
}

g_internal String8
city_http_call_wrapper(Arena* arena, String8 query_str, HTTP_RequestParams* params)
{
    DEBUG_LOG("DataFetch: Fetching data from overpass-api.de\n");
    String8 host = S("http://overpass-api.de");
    String8 path = S("/api/interpreter");

    const U32 retry_count = 3;
    S32 retries_left = retry_count;
    B32 http_success = false;
    U32 retry_time_interval_ms = 2000;
    HTTP_Response response;
    do
    {
        response = HTTP_Request(arena, host, path, query_str, params);

        if (response.good && response.code == HTTP_StatusCode_OK)
        {
            http_success = true;
            break;
        }

        DEBUG_LOG("DataFetch: Retrying...\n");
        os_sleep_milliseconds(retry_time_interval_ms);
        retries_left -= 1;
    } while (retries_left >= 0);

    if (http_success == false)
    {
        exit_with_error("DataFetch: http request did not succeed after %d retries\n"
                        "Failed with error code: %d\n"
                        "Error message: %s\n",
                        retry_count, response.code, response.body.str);
    }

    return response.body;
}

g_internal void
city_cache_write(String8 cache_file, String8 cache_meta_file, String8 content, String8 hash_content)
{
    ScratchScope scratch = ScratchScope(0, 0);
    // ~mgj: write to cache file
    OS_Handle file_write_handle = os_file_open(OS_AccessFlag_Write, cache_file);
    U64 bytes_written = OS_FileWrite(file_write_handle, r1u64(0, content.size), content.str);
    if (bytes_written != content.size)
    {
        DEBUG_LOG("DataFetch: Was not able to write OSM data to cache\n");
    }
    os_file_close(file_write_handle);

    // ~mgj: write to cache meta file
    if (content.size > 0)
    {
        U64 new_hash = HashU64FromStr8(hash_content);
        U64 timestamp = os_now_unix();
        String8 meta_str = PushStr8F(scratch.arena, "%llu\t%llu", new_hash, timestamp);

        OS_Handle file_write_handle_meta = os_file_open(OS_AccessFlag_Write, cache_meta_file);
        U64 bytes_written_meta =
            OS_FileWrite(file_write_handle_meta, r1u64(0, meta_str.size), meta_str.str);
        if (bytes_written_meta != meta_str.size)
        {
            DEBUG_LOG("DataFetch: Was not able to write meta data to cache\n");
        }
        os_file_close(file_write_handle_meta);
    }
}

g_internal Result<String8>
city_cache_read(Arena* arena, String8 cache_file, String8 cache_meta_file, String8 hash_input)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    B32 read_from_cache = false;
    String8 str = {};

    if (os_file_path_exists(cache_file))
    {
        read_from_cache = true;
        if (os_file_path_exists(cache_meta_file) == false)
        {
            read_from_cache = false;
        }
    }

    if (read_from_cache)
    {
        OS_Handle file_handle = os_file_open(OS_AccessFlag_Read, cache_file);
        FileProperties file_props = os_properties_from_file(file_handle);

        str = push_str8_fill_byte(arena, file_props.size, 0);
        U64 total_read_size = os_file_read(file_handle, r1u64(0, file_props.size), str.str);

        if (total_read_size != file_props.size)
        {
            DEBUG_LOG("DataFetch: Could not read everything from cache\n");
        }
        os_file_close(file_handle);

        B32 needs_update = cache_needs_update(hash_input, cache_meta_file);
        read_from_cache = !needs_update;
    }

    Result<String8> res = {str, !read_from_cache};
    return res;
}

g_internal Vec3F32
HeightDimAdd(Vec2F32 pos, F32 height)
{
    return {pos.x, height, pos.y};
}

g_internal void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<r_Vertex3D> buffer, Buffer<U32> indices,
                U64 way_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx)
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

g_internal osm::UtmNode*
NodeUtmFind(osm::Network* node_ways, U64 node_id)
{
    U64 node_index = node_id % node_ways->utm_node_hashmap.size;
    osm::UtmNodeList* slot = &node_ways->utm_node_hashmap.data[node_index];
    osm::UtmNode* node = slot->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
            return node;
    }
    return &osm::g_road_node_utm;
}

g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way,
                           osm::Network* node_utm_structure)
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
        osm::UtmNode* node = road_cross_section->node;
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

        for (osm::WayNode* road_way_list = node->way_queue.first; road_way_list;
             road_way_list = road_way_list->next)
        {
            osm::Way* way = &road_way_list->way;
            F32 road_width =
                tag_value_get(scratch.arena, S("width"), road->default_road_width, way->tags);
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
                            adj_node->node = NodeUtmFind(node_utm_structure, prev_node_id);
                            SLLStackPush(adj_node_ll, adj_node);
                        }

                        if (node_idx < way->node_count - 1)
                        {
                            U32 next_node_idx = node_idx + 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 next_node_id = way->node_ids[next_node_idx];
                            adj_node->node = NodeUtmFind(node_utm_structure, next_node_id);
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
                    if (ui_line_intersect(
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

                    if (ui_line_intersect(
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
                    if (ui_line_intersect(
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

                    if (ui_line_intersect(
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

// ~mgj: Cars
g_internal Rng1F32
CarCenterHeightOffset(Buffer<gltfw_Vertex3D> vertices)
{
    F32 highest_value = 0;
    for (U64 i = 0; i < vertices.size; i++)
    {
        highest_value = Max(highest_value, vertices.data[i].pos.z);
    }

    F32 lowest_value = highest_value;
    for (U64 i = 0; i < vertices.size; i++)
    {
        lowest_value = Min(lowest_value, vertices.data[i].pos.z);
    }

    return r1f32(lowest_value, highest_value);
}

g_internal CarSim*
CarSimCreate(String8 asset_path, String8 texture_path, U32 car_count, Road* road)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();

    CarSim* car_sim = PushStruct(arena, CarSim);
    car_sim->arena = arena;

    // parse gltf file
    String8 gltf_path = Str8PathFromStr8List(scratch.arena, {asset_path, S("cars/scene.gltf")});
    CgltfResult parsed_result = gltfw_gltf_read(arena, gltf_path, S("Car.013"));
    car_sim->sampler_info = sampler_from_cgltf_sampler(parsed_result.sampler);
    car_sim->vertex_buffer =
        r_buffer_info_from_template_buffer(parsed_result.vertex_buffer, R_BufferType_Vertex);
    car_sim->index_buffer =
        r_buffer_info_from_template_buffer(parsed_result.index_buffer, R_BufferType_Index);

    car_sim->texture_path = Str8PathFromStr8List(arena, {texture_path, S("car_collection.ktx2")});
    car_sim->texture_handle = r_texture_load_async(&car_sim->sampler_info, car_sim->texture_path,
                                                   R_PipelineUsageType_3DInstanced);
    car_sim->vertex_handle = r_buffer_load(&car_sim->vertex_buffer);
    car_sim->index_handle = r_buffer_load(&car_sim->index_buffer);

    car_sim->car_center_offset = CarCenterHeightOffset(parsed_result.vertex_buffer);
    car_sim->cars = BufferAlloc<Car>(arena, car_count);

    for (U32 i = 0; i < car_count; ++i)
    {
        osm::UtmNode* source_node = osm::random_utm_node_get();
        osm::UtmNode* target_node = osm::random_neighbour_node_get(source_node);
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

g_internal void
car_sim_destroy(CarSim* car_sim)
{
    r_buffer_destroy(car_sim->vertex_handle);
    r_buffer_destroy(car_sim->index_handle);
    r_texture_destroy(car_sim->texture_handle);
    ArenaRelease(car_sim->arena);
}

g_internal Buffer<r_Model3DInstance>
CarUpdate(Arena* arena, CarSim* car, F32 time_delta)
{
    Buffer<r_Model3DInstance> instance_buffer =
        BufferAlloc<r_Model3DInstance>(arena, car->cars.size);
    r_Model3DInstance* instance;
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
            osm::UtmNode* new_target = osm::random_neighbour_node_get(car_info->target);
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

g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, F32 road_height, Rng2F64 bbox,
                 r_SamplerInfo* sampler_info)
{
    osm::Network* network = osm::g_network;
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();
    Buildings* buildings = PushStruct(arena, Buildings);
    buildings->arena = arena;
    buildings->cache_file_name = PushStr8Copy(arena, S("openapi_node_ways_buildings.json"));

    {
        HTTP_RequestParams params = {};
        params.method = HTTP_Method_Post;
        params.content_type = S("text/html");

        String8 query = S(R"(data=
        [out:json] [timeout:25];
        (
          way["building"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )");

        String8 query_str = PushStr8F(scratch.arena, (char*)query.str, bbox.min.y, bbox.min.x,
                                      bbox.max.y, bbox.max.x);
        String8 cache_data_file =
            Str8PathFromStr8List(scratch.arena, {cache_path, buildings->cache_file_name});
        String8 cache_meta_file = PushStr8Cat(scratch.arena, cache_data_file, S(".meta"));

        String8 input_str = str8_from_bbox(scratch.arena, bbox);

        Result<String8> cache_read_result =
            city_cache_read(scratch.arena, cache_data_file, cache_meta_file, input_str);
        String8 http_data = cache_read_result.v;
        if (cache_read_result.err)
        {
            http_data = city_http_call_wrapper(scratch.arena, query_str, &params);
            city_cache_write(cache_data_file, cache_meta_file, http_data, input_str);
        }
        osm::RoadNodeParseResult json_result =
            wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);

        B8 error = true;
        while (error && json_result.error)
        {
            ERROR_LOG("BuildingsCreate: Failed to parse OSM node data from json file\n");
            http_data = city_http_call_wrapper(scratch.arena, query_str, &params);
            if (http_data.size)
            {
                json_result = wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);
                if (json_result.error == false)
                {
                    city_cache_write(cache_data_file, cache_meta_file, http_data, input_str);
                    error = false;
                }
            }
        }

        osm::structure_add(json_result.road_nodes, http_data, osm::OsmKeyType_Building);
    }

    buildings->facade_texture_path =
        Str8PathFromStr8List(arena, {texture_path, S("brick_wall.ktx2")});
    buildings->roof_texture_path =
        Str8PathFromStr8List(arena, {texture_path, S("concrete042A.ktx2")});
    r_Handle facade_texture_handle =
        r_texture_load_async(sampler_info, buildings->facade_texture_path, R_PipelineUsageType_3D);
    r_Handle roof_texture_handle =
        r_texture_load_async(sampler_info, buildings->roof_texture_path, R_PipelineUsageType_3D);

    BuildingRenderInfo render_info;
    city::BuildingsBuffersCreate(arena, road_height, &render_info, network);
    r_BufferInfo vertex_buffer_info =
        r_buffer_info_from_template_buffer(render_info.vertex_buffer, R_BufferType_Vertex);
    r_BufferInfo index_buffer_info =
        r_buffer_info_from_template_buffer(render_info.index_buffer, R_BufferType_Index);

    r_Handle vertex_handle = r_buffer_load(&vertex_buffer_info);
    r_Handle index_handle = r_buffer_load(&index_buffer_info);

    buildings->roof_model_handles = {.vertex_buffer_handle = vertex_handle,
                                     .index_buffer_handle = index_handle,
                                     .texture_handle = roof_texture_handle,
                                     .index_count = render_info.roof_index_count,
                                     .index_offset = render_info.roof_index_offset};
    buildings->facade_model_handles = {.vertex_buffer_handle = vertex_handle,
                                       .index_buffer_handle = index_handle,
                                       .texture_handle = facade_texture_handle,
                                       .index_count = render_info.facade_index_count,
                                       .index_offset = render_info.facade_index_offset};

    return buildings;
}

g_internal void
building_destroy(Buildings* building)
{
    r_buffer_destroy(building->roof_model_handles.vertex_buffer_handle);
    r_buffer_destroy(building->roof_model_handles.index_buffer_handle);
    r_texture_destroy(building->roof_model_handles.texture_handle);
    r_texture_destroy(building->facade_model_handles.texture_handle);
    ArenaRelease(building->arena);
}
g_internal F32
Cross2F32ZComponent(Vec2F32 a, Vec2F32 b)
{
    return a.x * b.y - a.y * b.x;
}
g_internal B32
AreTwoConnectedLineSegmentsCollinear(Vec2F32 prev, Vec2F32 cur, Vec2F32 next)
{
    Vec2F32 ba = Sub2F32(prev, cur);
    Vec2F32 ac = Sub2F32(next, prev);

    F32 cross_product_z = Cross2F32ZComponent(ba, ac);
    B32 is_collinear = false;
    if (cross_product_z == 0)
    {
        is_collinear = true;
    }
    return is_collinear;
}
// TODO: Built the roof, which requires a way to divide the concave polygons (that are the
// buildings) and divide it into convex parts
g_internal void
BuildingsBuffersCreate(Arena* arena, F32 road_height, BuildingRenderInfo* out_render_info,
                       osm::Network* node_utm_structure)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<osm::Way> ways = node_utm_structure->ways_arr[osm::OsmKeyType_Building];
    F32 building_height = 3;

    // ~mgj: Calculate vertex buffer size based on node count
    U32 total_vertex_count = 0;
    U32 total_index_count = 0;

    for (U32 i = 0; i < ways.size; i++)
    {
        osm::Way* way = &ways.data[i];
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

    Buffer<r_Vertex3D> vertex_buffer = BufferAlloc<r_Vertex3D>(scratch.arena, total_vertex_count);
    Buffer<U32> index_buffer = BufferAlloc<U32>(scratch.arena, total_index_count);

    U32 base_index_idx = 0;
    U32 base_vertex_idx = 0;
    for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
    {
        osm::Way* way = &ways.data[way_idx];

        // ~mgj: Add Vertices and Indices for the sides of building
        for (U32 node_idx = 0, vert_idx = base_vertex_idx, index_idx = base_index_idx;
             node_idx < way->node_count - 1; node_idx++, vert_idx += 4, index_idx += 6)
        {
            osm::UtmNode* utm_node = NodeUtmFind(node_utm_structure, way->node_ids[node_idx]);
            osm::UtmNode* utm_node_next =
                NodeUtmFind(node_utm_structure, way->node_ids[node_idx + 1]);
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
    for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
    {
        osm::Way* way = &ways.data[way_idx];
        Buffer<osm::UtmNode*> buildings_utm_node_buffer =
            BufferAlloc<osm::UtmNode*>(scratch.arena, way->node_count - 1);
        for (U32 idx = 0; idx < way->node_count - 1; idx += 1)
        {
            buildings_utm_node_buffer.data[idx] =
                NodeUtmFind(node_utm_structure, way->node_ids[idx]);
        }

        // ~mgj: ignore collinear line segments
        Buffer<osm::UtmNode*> final_utm_node_buffer =
            BufferAlloc<osm::UtmNode*>(scratch.arena, buildings_utm_node_buffer.size);
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
            osm::UtmNode* node_utm = final_utm_node_buffer.data[idx];
            node_pos_buffer.data[idx] = node_utm->pos;
        }

        Buffer<U32> polygon_index_buffer = EarClipping(scratch.arena, node_pos_buffer);
        if (polygon_index_buffer.size > 0)
        {
            // vertex buffer fill
            for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
            {
                osm::UtmNode* node_utm = final_utm_node_buffer.data[idx];
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

    Buffer<r_Vertex3D> vertex_buffer_final = BufferAlloc<r_Vertex3D>(arena, base_vertex_idx);
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

g_internal Direction
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

g_internal Buffer<U32>
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
g_internal B32
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

g_internal void
NodeBufferPrintDebug(Buffer<Vec2F32> node_buffer)
{
    DEBUG_LOG("Error in ear clipping algo. Expecting vertex_count-2 number of triangles\n"
              "The following vertices are the problem: \n");
    for (U32 pt_idx = 0; pt_idx < node_buffer.size; pt_idx++)
    {
        printf("%d, %f, %f\n", pt_idx, node_buffer.data[pt_idx].x, node_buffer.data[pt_idx].y);
    }
}

g_internal Buffer<U32>
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
            B32 is_ear = true;
            for (U32 test_i = 0; test_i < index_buffer.size - 3; test_i++)
            {
                U32 test_node_buffer_idx =
                    index_buffer.data[(next_index_buffer_idx + test_i + 1) % index_buffer.size];
                Vec2F32 test_point = node_buffer.data[test_node_buffer_idx];

                if (PointInTriangle(prev, ear, next, test_point))
                {
                    is_ear = false;
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
            DEBUG_LOG("Error in EarClipping: two line segments are collinear");
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

g_internal void
land_destroy(r_Model3DPipelineDataList list)
{
    for (r_Model3DPipelineDataNode* data = list.first; data; data = data->next)
    {
        r_buffer_destroy(data->handles.index_buffer_handle);
        r_buffer_destroy(data->handles.vertex_buffer_handle);
        r_texture_destroy(data->handles.texture_handle);
    }
}

g_internal r_Model3DPipelineDataList
land_create(Arena* arena, String8 glb_path)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    gltfw_Result glb_data = gltfw_glb_read(arena, glb_path);

    // create render handles
    Buffer<r_Handle> tex_handles = BufferAlloc<r_Handle>(scratch.arena, glb_data.textures.size);
    {
        for (U32 i = 0; i < glb_data.textures.size; ++i)
        {
            gltfw_Texture* texture = &glb_data.textures.data[i];
            r_SamplerInfo sampler_info = city::sampler_from_cgltf_sampler(texture->sampler);
            tex_handles.data[i] = r_texture_handle_create(&sampler_info, R_PipelineUsageType_3D);
            r_texture_gpu_upload_sync(tex_handles.data[i], texture->tex_buf);
        }
    }

    // create vertex and index buffers
    r_Model3DPipelineDataList handles_list = {};
    {
        for (gltfw_Primitive* primitive = glb_data.primitives.first; primitive;
             primitive = primitive->next)
        {
            r_BufferInfo vertex_buffer =
                r_buffer_info_from_template_buffer(primitive->vertices, R_BufferType_Vertex);
            r_BufferInfo index_buffer =
                r_buffer_info_from_template_buffer(primitive->indices, R_BufferType_Index);

            r_Handle vertex_handle = r_buffer_load(&vertex_buffer);
            r_Handle index_handle = r_buffer_load(&index_buffer);
            r_Handle texture_handle = tex_handles.data[primitive->tex_idx];

            r_Model3DPipelineDataNode* node = PushStruct(arena, r_Model3DPipelineDataNode);
            node->handles = {.vertex_buffer_handle = vertex_handle,
                             .index_buffer_handle = index_handle,
                             .texture_handle = texture_handle,
                             .index_count = primitive->indices.size,
                             .index_offset = 0};
            SLLQueuePush(handles_list.first, handles_list.last, node);
        }
    }
    return handles_list;
}

g_internal r_SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler)
{
    r_Filter min_filter = R_Filter_Nearest;
    r_Filter mag_filter = R_Filter_Nearest;
    r_MipMapMode mipmap_mode = R_MipMapMode_Nearest;
    r_SamplerAddressMode address_mode_u = R_SamplerAddressMode_Repeat;
    r_SamplerAddressMode address_mode_v = R_SamplerAddressMode_Repeat;

    switch (sampler.min_filter)
    {
        default: break;
        case cgltf_filter_type_nearest:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Linear;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Linear;
        }
        break;
    }

    if (sampler.mag_filter != sampler.min_filter)
    {
        switch (sampler.mag_filter)
        {
            default: break;
            case cgltf_filter_type_nearest_mipmap_nearest:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_nearest:
            {
                mag_filter = R_Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest_mipmap_linear:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_linear:
            {
                mag_filter = R_Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear:
            {
                mag_filter = R_Filter_Linear;
            }
            break;

                break;
        }
    }

    switch (sampler.wrap_s)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_u = R_SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_u = R_SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_u = R_SamplerAddressMode_MirroredRepeat;
            break;
    }
    switch (sampler.wrap_t)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_v = R_SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_v = R_SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_v = R_SamplerAddressMode_MirroredRepeat;
            break;
    }

    r_SamplerInfo sampler_info = {.min_filter = min_filter,
                                  .mag_filter = mag_filter,
                                  .mip_map_mode = mipmap_mode,
                                  .address_mode_u = address_mode_u,
                                  .address_mode_v = address_mode_v};

    return sampler_info;
}

g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox)
{
    String8 str = {.str = (U8*)&bbox, .size = sizeof(Rng2F64)};
    String8 str_copy = PushStr8Copy(arena, str);
    return str_copy;
}

g_internal Road*
road_create(String8 texture_path, String8 cache_path, Rng2F64 bbox, r_SamplerInfo* sampler_info)
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
    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;
    params.content_type = S("text/html");

    String8 query_str =
        PushStr8F(scratch.arena, (char*)query.str, bbox.min.y, bbox.min.x, bbox.max.y, bbox.max.x);
    String8 cache_data_file =
        Str8PathFromStr8List(scratch.arena, {cache_path, road->openapi_data_file_name});
    String8 cache_meta_file = PushStr8Cat(scratch.arena, cache_data_file, S(".meta"));

    String8 input_str = str8_from_bbox(scratch.arena, bbox);

    Result<String8> cache_read_result =
        city::city_cache_read(scratch.arena, cache_data_file, cache_meta_file, input_str);
    String8 http_data = cache_read_result.v;
    if (cache_read_result.err)
    {
        http_data = city::city_http_call_wrapper(scratch.arena, query_str, &params);
        city::city_cache_write(cache_data_file, cache_meta_file, http_data, input_str);
    }
    osm::RoadNodeParseResult json_result =
        wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);

    B8 error = true;
    while (error && json_result.error)
    {
        ERROR_LOG("RoadCreate: Failed to create Road Data Structure\n Retrying...\n");
        http_data = city::city_http_call_wrapper(scratch.arena, query_str, &params);
        if (http_data.size)
        {
            json_result = wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);
            if (json_result.error == false)
            {
                city::city_cache_write(cache_data_file, cache_meta_file, http_data, input_str);
                error = false;
            }
        }
    }

    Buffer<osm::RoadNodeList> node_hashmap = json_result.road_nodes;
    osm::structure_add(node_hashmap, http_data, osm::OsmKeyType_Road);

    EdgeStructure edge_structure = city::road_edge_structure_create(road->arena);
    road->edge_map = edge_structure.edge_map;

    city::RenderBuffers render_buffers = city::road_render_buffers_create(
        road->arena, edge_structure.edges, road->default_road_width, road->road_height);

    r_BufferInfo vertex_buffer_info =
        r_buffer_info_from_template_buffer(render_buffers.vertices, R_BufferType_Vertex);
    r_BufferInfo index_buffer_info =
        r_buffer_info_from_template_buffer(render_buffers.indices, R_BufferType_Index);

    road->texture_path = Str8PathFromStr8List(road->arena, {texture_path, S("road_texture.ktx2")});

    r_Handle texture_handle =
        r_texture_load_async(sampler_info, road->texture_path, R_PipelineUsageType_3D);
    r_Handle vertex_handle = r_buffer_load(&vertex_buffer_info);
    r_Handle index_handle = r_buffer_load(&index_buffer_info);

    road->handles = {vertex_handle, index_handle, texture_handle, render_buffers.indices.size, 0};

    return road;
}

g_internal EdgeStructure
road_edge_structure_create(Arena* arena)
{
    osm::Network* network = osm::g_network;
    Buffer<osm::Way> way_buf = network->ways_arr[osm::OsmKeyType_Road];

    ChunkList<RoadEdge>* chunk_list = chunk_list_create<RoadEdge>(arena, 1024);
    for (const osm::Way& way : way_buf)
    {
        RoadEdge* prev_edge = 0;
        for (S32 node_idx = 1; node_idx < way.node_count; node_idx++)
        {
            U64 prev_node_id = way.node_ids[node_idx - 1];
            U64 node_id = way.node_ids[node_idx];

            RoadEdge* road_edge = chunk_list_get_next(arena, chunk_list);
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

    Buffer<RoadEdge> road_edge_buf = buffer_from_chunk_list(arena, chunk_list);
    Map<S64, RoadEdge*>* road_edge_map = map_create<S64, RoadEdge*>(arena, 1024);
    for (RoadEdge* edge = road_edge_buf.begin(); edge < road_edge_buf.end(); edge++)
    {
        map_insert(road_edge_map, edge->id, edge);
    }

    EdgeStructure edge_structure = {.edges = road_edge_buf, .edge_map = *road_edge_map};
    return edge_structure;
}

g_internal neta_Edge*
neta_edge_from_road_edge(RoadEdge* road_edge, Map<S64, neta_EdgeList>* edge_list_map)
{
    S64 from_id = road_edge->node_id_from;
    S64 to_id = road_edge->node_id_to;

    osm::UtmNode* from_node = osm::utm_node_find(from_id);
    osm::UtmNode* to_node = osm::utm_node_find(to_id);
    Vec2F64 from_node_coord = vec_2f64(from_node->pos.x, from_node->pos.y);
    Vec2F64 to_node_coord = vec_2f64(to_node->pos.x, to_node->pos.y);

    S64 way_id = road_edge->way_id;
    neta_EdgeList* edge_list = map_get(edge_list_map, way_id);

    F64 smallest_dist = max_f64;
    neta_Edge* chosen_edge = {};
    for (neta_EdgeNode* edge_node = edge_list->first; edge_node; edge_node = edge_node->next)
    {
        neta_Edge* edge = edge_node->edge;
        for (Vec2F64& coord : edge->coords)
        {
            F64 from_dist = dist_2f64(coord, from_node_coord);
            F64 to_dist = dist_2f64(coord, to_node_coord);
            F64 closest_dist = Min(from_dist, to_dist);
            if (closest_dist < smallest_dist)
            {
                smallest_dist = closest_dist;
                chosen_edge = edge;
            }
        }
    }

    return chosen_edge;
}

} // namespace city
