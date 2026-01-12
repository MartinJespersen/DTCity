namespace city
{

g_internal void
road_destroy(Road* road)
{
    for (U32 i = 0; i < ArrayCount(road->handles); ++i)
    {
        render::buffer_destroy(road->handles[i].vertex_buffer_handle);
        render::buffer_destroy(road->handles[i].index_buffer_handle);
        render::texture_destroy(road->handles[i].texture_handle);
    }

    ArenaRelease(road->arena);
}

g_internal void
RoadSegmentFromTwoRoadNodes(RoadSegment* out_road_segment, osm::UtmLocation node_0,
                            osm::UtmLocation node_1, F32 road_width)
{
    Vec2F32 road_0_pos = node_0.pos;
    Vec2F32 road_1_pos = node_1.pos;

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
    Vec2F32 shared_center = in_out_road_segment_0->end.node.pos;

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
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Buffer<render::Vertex3D> vertex_buffer =
        BufferAlloc<render::Vertex3D>(arena, edge_buffer.size * 4);
    Buffer<U32> index_buffer = BufferAlloc<U32>(arena, edge_buffer.size * 6);

    U32 cur_vertex_idx = 0;
    U32 cur_index_idx = 0;
    for (auto& edge : edge_buffer)
    {
        osm::UtmLocation start_node = city::utm_location_find(edge.node_id_from);
        osm::UtmLocation end_node = city::utm_location_find(edge.node_id_to);
        osm::WayNode* way_node = osm::way_find(edge.way_id);
        osm::Way* way = &way_node->way;

        F32 road_width = tag_value_get(scratch.arena, S("width"), default_road_width, way->tags);
        if (road_width != default_road_width)
        {
            printf("Road width is not default: %f\n", road_width);
        }

        RoadSegment road_segment;
        RoadSegmentFromTwoRoadNodes(&road_segment, start_node, end_node, default_road_width);

        RoadEdge* prev_edge = edge.prev;
        if (prev_edge)
        {
            osm::UtmLocation start_node_prev = city::utm_location_find(prev_edge->node_id_from);
            osm::UtmLocation end_node_prev = city::utm_location_find(prev_edge->node_id_to);
            RoadSegment road_segment_prev;
            RoadSegmentFromTwoRoadNodes(&road_segment_prev, start_node_prev, end_node_prev,
                                        road_width);
            RoadSegmentConnectionFromTwoRoadSegments(&road_segment_prev, &road_segment, road_width);
        }

        RoadEdge* next_edge = edge.next;
        if (next_edge)
        {
            osm::UtmLocation start_node_next = city::utm_location_find(next_edge->node_id_from);
            osm::UtmLocation end_node_next = city::utm_location_find(next_edge->node_id_to);
            RoadSegment road_segment_next;
            RoadSegmentFromTwoRoadNodes(&road_segment_next, start_node_next, end_node_next,
                                        road_width);
            RoadSegmentConnectionFromTwoRoadSegments(&road_segment, &road_segment_next, road_width);
        }

        quad_to_buffer_add(&road_segment, vertex_buffer, index_buffer, edge.id, road_height,
                           &cur_vertex_idx, &cur_index_idx);
    }

    city::RenderBuffers render_buffers = {.vertices = vertex_buffer, .indices = index_buffer};
    return render_buffers;
}

g_internal U64
hash_u64_from_str8(String8 str)
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
            U64 cur_file_hash = hash_u64_from_str8(cache_data_file);

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
    prof_scope_marker;
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
cache_write(String8 cache_file, String8 cache_meta_file, String8 content, String8 hash_content)
{
    prof_scope_marker;
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
        U64 new_hash = hash_u64_from_str8(hash_content);
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
cache_read(Arena* arena, String8 cache_file, String8 cache_meta_file, String8 hash_input)
{
    prof_scope_marker;
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
height_dim_add(Vec2F32 pos, F32 height)
{
    Vec3F32 result = {pos.x, pos.y, height};
    return result;
}

g_internal void
quad_to_buffer_add(RoadSegment* road_segment, Buffer<render::Vertex3D> buffer, Buffer<U32> indices,
                   U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx)
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

    Vec2U32 id = {.u64 = edge_id};
    Vec4F32 color = {0.0f, 0.0f, 0.0f, 0.0f};

    // quad of vertices
    buffer.data[base_vertex_idx] = {.pos = height_dim_add(road_segment->start.top, road_height),
                                    .uv = {uv_x_top, uv_y_start},
                                    .object_id = id,
                                    .color = color};
    buffer.data[base_vertex_idx + 1] = {.pos = height_dim_add(road_segment->start.btm, road_height),
                                        .uv = {uv_x_btm, uv_y_start},
                                        .object_id = id,
                                        .color = color};
    buffer.data[base_vertex_idx + 2] = {.pos = height_dim_add(road_segment->end.top, road_height),
                                        .uv = {uv_x_top, uv_y_end},
                                        .object_id = id,
                                        .color = color};
    buffer.data[base_vertex_idx + 3] = {.pos = height_dim_add(road_segment->end.btm, road_height),
                                        .uv = {uv_x_btm, uv_y_end},
                                        .object_id = id,
                                        .color = color};

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

g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way)
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
        U64 node_id = road_cross_section->node.id;
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

        osm::Node* node = osm::node_get(node_id);
        osm::UtmLocation node_loc = city::utm_location_find(node->id);
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
                            adj_node->node = osm::node_get(prev_node_id);
                            SLLStackPush(adj_node_ll, adj_node);
                        }

                        if (node_idx < way->node_count - 1)
                        {
                            U32 next_node_idx = node_idx + 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 next_node_id = way->node_ids[next_node_idx];
                            adj_node->node = osm::node_get(next_node_id);
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
                    osm::UtmLocation adj_node_loc =
                        city::utm_location_find(adj_node_item->node->id);
                    RoadSegmentFromTwoRoadNodes(&crossing_road_segment, adj_node_loc, node_loc,
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
car_center_height_offset(Buffer<gltfw_Vertex3D> vertices)
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

g_internal Vec2F32
world_position_offset_adjust(Vec2F32 position)
{
    ui::Camera* camera = dt_ctx_get()->camera;
    Vec2F32 new_pos =
        vec_2f32(position.x - camera->world_offset.x, position.y - camera->world_offset.y);
    return new_pos;
}

g_internal osm::UtmLocation
utm_location_find(U64 node_id)
{
    osm::UtmLocation node_loc = osm::utm_location_get(node_id);
    Vec2F32 new_pos = world_position_offset_adjust(node_loc.pos);
    osm::UtmLocation new_loc = osm::utm_location_create(node_loc.id, new_pos);
    return new_loc;
}

g_internal osm::UtmLocation
random_utm_road_node_get()
{
    osm::NodeId node_id = osm::random_node_id_from_type_get(osm::WayType_Road);
    Assert(node_id != 0);
    osm::UtmLocation node_loc = osm::utm_location_get(node_id);
    Vec2F32 new_pos = world_position_offset_adjust(node_loc.pos);
    osm::UtmLocation new_loc = osm::utm_location_create(node_loc.id, new_pos);
    return new_loc;
}

g_internal CarSim*
car_sim_create(String8 asset_path, String8 texture_path, U32 car_count, Road* road)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = ArenaAlloc();

    CarSim* car_sim = PushStruct(arena, CarSim);
    car_sim->arena = arena;

    // parse gltf file
    String8 gltf_path = str8_path_from_str8_list(scratch.arena, {asset_path, S("cars/scene.gltf")});
    CgltfResult parsed_result = gltfw_gltf_read(scratch.arena, gltf_path, S("Car.013"));
    car_sim->sampler_info = sampler_from_cgltf_sampler(parsed_result.sampler);
    Buffer<render::Vertex3D> vertex_buffer =
        vertex_3d_from_gltfw_vertex(arena, parsed_result.vertex_buffer);
    car_sim->vertex_buffer =
        render::buffer_info_from_vertex_3d_buffer(vertex_buffer, render::BufferType_Vertex);
    Buffer<U32> index_buffer = buffer_arena_copy(arena, parsed_result.index_buffer);
    car_sim->index_buffer =
        render::buffer_info_from_u32_index_buffer(index_buffer, render::BufferType_Index);

    car_sim->texture_path =
        str8_path_from_str8_list(arena, {texture_path, S("car_collection.ktx2")});
    car_sim->texture_handle = render::texture_load_async(
        &car_sim->sampler_info, car_sim->texture_path, render::PipelineUsageType_3DInstanced);
    car_sim->vertex_handle = render::buffer_load(&car_sim->vertex_buffer);
    car_sim->index_handle = render::buffer_load(&car_sim->index_buffer);

    car_sim->car_center_offset = car_center_height_offset(parsed_result.vertex_buffer);
    car_sim->cars = BufferAlloc<Car>(arena, car_count);

    for (U32 i = 0; i < car_count; ++i)
    {
        osm::UtmLocation source_loc = city::random_utm_road_node_get();
        osm::Node* source_node = osm::node_get(source_loc.id);
        osm::Node* target_node = osm::random_neighbour_node_get(source_node);
        osm::UtmLocation target_loc = city::utm_location_find(target_node->id);
        city::Car* car = &car_sim->cars.data[i];
        car->source_loc = source_loc;
        car->target_loc = target_loc;
        car->speed = 10.0f;
        car->cur_pos =
            height_dim_add(source_loc.pos, road->road_height - car_sim->car_center_offset.min);
        Vec2F32 dir =
            vec_2f32(target_loc.pos.x - source_loc.pos.x, target_loc.pos.y - source_loc.pos.y);
        car->dir = normalize_3f32(height_dim_add(dir, 0));
    }
    ui::Camera* camera = dt_ctx_get()->camera;

    return car_sim;
}

g_internal void
car_sim_destroy(CarSim* car_sim)
{
    render::buffer_destroy(car_sim->vertex_handle);
    render::buffer_destroy(car_sim->index_handle);
    render::texture_destroy(car_sim->texture_handle);
    ArenaRelease(car_sim->arena);
}

g_internal Buffer<render::Model3DInstance>
car_sim_update(Arena* arena, CarSim* car, F64 time_delta)
{
    Buffer<render::Model3DInstance> instance_buffer =
        BufferAlloc<render::Model3DInstance>(arena, car->cars.size);

    render::Model3DInstance* instance;
    city::Car* car_info;

    F32 car_speed_default = 5.0f; // m/s
    for (U32 car_idx = 0; car_idx < car->cars.size; car_idx++)
    {
        instance = &instance_buffer.data[car_idx];
        car_info = &car->cars.data[car_idx];

        Vec3F32 target_pos = height_dim_add(car_info->target_loc.pos, car_info->cur_pos.z);
        Vec3F32 new_pos =
            add_3f32(car_info->cur_pos,
                     scale_3f32(scale_3f32(car_info->dir, car_speed_default), (F32)time_delta));

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
            osm::Node* node = osm::random_neighbour_node_get(car_info->target_loc.id);
            osm::UtmLocation new_target_loc = city::utm_location_find(node->id);
            Vec3F32 new_target_pos = height_dim_add(new_target_loc.pos, car_info->cur_pos.z);
            Vec3F32 new_dir = normalize_3f32(sub_3f32(new_target_pos, new_pos));
            car_info->dir = new_dir;
            car_info->source_loc = car_info->target_loc;
            car_info->target_loc = new_target_loc;
        }

        glm::vec3 dir = glm::vec3(car_info->dir.x, car_info->dir.y, car_info->dir.z);
        glm::vec3 y_basis = dir;
        y_basis *= -1; // In model space, the front of the car ws in the negative y direction
        glm::vec3 x_basis = glm::vec3(y_basis.y, -y_basis.x, 0);
        glm::vec3 z_basis = glm::cross(x_basis, y_basis);

        instance->x_basis = glm::vec4(x_basis, 0);
        instance->y_basis = glm::vec4(y_basis, 0);
        instance->z_basis = glm::vec4(z_basis, 0);
        instance->w_basis = {glm::vec3(new_pos.x, new_pos.y, new_pos.z), 1};

        car_info->cur_pos = new_pos;
    }
    return instance_buffer;
}
// ~mgj: Buildings

g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, F32 road_height, Rng2F64 bbox,
                 render::SamplerInfo* sampler_info)
{
    prof_scope_marker;
    osm::Network* network = osm::g_network;
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();
    Buildings* buildings = PushStruct(arena, Buildings);
    buildings->arena = arena;
    buildings->cache_file_name = push_str8_copy(arena, S("openapi_node_ways_buildings.json"));

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
            str8_path_from_str8_list(scratch.arena, {cache_path, buildings->cache_file_name});
        String8 cache_meta_file = PushStr8Cat(scratch.arena, cache_data_file, S(".meta"));

        String8 input_str = str8_from_bbox(scratch.arena, bbox);

        Result<String8> cache_read_result =
            cache_read(scratch.arena, cache_data_file, cache_meta_file, input_str);
        String8 http_data = cache_read_result.v;
        if (cache_read_result.err)
        {
            http_data = city_http_call_wrapper(scratch.arena, query_str, &params);
            cache_write(cache_data_file, cache_meta_file, http_data, input_str);
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
                    cache_write(cache_data_file, cache_meta_file, http_data, input_str);
                    error = false;
                }
            }
        }

        osm::structure_add(json_result.road_nodes, http_data, osm::WayType_Building);
    }

    buildings->facade_texture_path =
        str8_path_from_str8_list(arena, {texture_path, S("brick_wall.ktx2")});
    buildings->roof_texture_path =
        str8_path_from_str8_list(arena, {texture_path, S("concrete042A.ktx2")});
    render::Handle facade_texture_handle = render::texture_load_async(
        sampler_info, buildings->facade_texture_path, render::PipelineUsageType_3D);
    render::Handle roof_texture_handle = render::texture_load_async(
        sampler_info, buildings->roof_texture_path, render::PipelineUsageType_3D);

    BuildingRenderInfo render_info;
    city::buildings_buffers_create(arena, road_height, &render_info);
    render::BufferInfo vertex_buffer_info = render::buffer_info_from_vertex_3d_buffer(
        render_info.vertex_buffer, render::BufferType_Vertex);
    render::BufferInfo index_buffer_info = render::buffer_info_from_u32_index_buffer(
        render_info.index_buffer, render::BufferType_Index);

    render::Handle vertex_handle = render::buffer_load(&vertex_buffer_info);
    render::Handle index_handle = render::buffer_load(&index_buffer_info);

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
    render::buffer_destroy(building->roof_model_handles.vertex_buffer_handle);
    render::buffer_destroy(building->roof_model_handles.index_buffer_handle);
    render::texture_destroy(building->roof_model_handles.texture_handle);
    render::texture_destroy(building->facade_model_handles.texture_handle);
    ArenaRelease(building->arena);
}
g_internal F64
cross_2f64_z_component(Vec2F64 a, Vec2F64 b)
{
    return a.x * b.y - a.y * b.x;
}
g_internal B32
AreTwoConnectedLineSegmentsCollinear(Vec2F64 prev, Vec2F64 cur, Vec2F64 next)
{
    Vec2F64 ba = sub_2f64(prev, cur);
    Vec2F64 ac = sub_2f64(next, prev);

    F64 cross_product_z = cross_2f64_z_component(ba, ac);
    B32 is_collinear = false;
    if (cross_product_z == 0)
    {
        is_collinear = true;
    }
    return is_collinear;
}

g_internal void
buildings_buffers_create(Arena* arena, F32 road_height, BuildingRenderInfo* out_render_info)
{
    prof_scope_marker;
    osm::Network* osm_network = osm::g_network;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<osm::Way> ways = osm_network->ways_arr[osm::WayType_Building];
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

    Buffer<render::Vertex3D> vertex_buffer =
        BufferAlloc<render::Vertex3D>(scratch.arena, total_vertex_count);
    Buffer<U32> index_buffer = BufferAlloc<U32>(scratch.arena, total_index_count);

    U32 base_index_idx = 0;
    U32 base_vertex_idx = 0;
    {
        prof_scope_marker_named("Facade Creation");

        for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
        {
            osm::Way* way = &ways.data[way_idx];

            // ~mgj: Add Vertices and Indices for the sides of building
            for (U32 node_idx = 0, vert_idx = base_vertex_idx, index_idx = base_index_idx;
                 node_idx < way->node_count - 1; node_idx++, vert_idx += 4, index_idx += 6)
            {
                osm::UtmLocation node_loc = city::utm_location_find(way->node_ids[node_idx]);
                osm::UtmLocation next_node_loc =
                    city::utm_location_find(way->node_ids[node_idx + 1]);
                F32 side_width = Length2F32(Sub2F32(node_loc.pos, next_node_loc.pos));

                Vec2U32 id = {.u64 = (U64)way->id};

                vertex_buffer.data[vert_idx] = {.pos = height_dim_add(node_loc.pos, road_height),
                                                .uv = {0.0f, 0.0f},
                                                .object_id = id};
                vertex_buffer.data[vert_idx + 1] = {
                    .pos = height_dim_add(node_loc.pos, road_height + building_height),
                    .uv = {0.0f, building_height},
                    .object_id = id};

                vertex_buffer.data[vert_idx + 2] = {
                    .pos = height_dim_add(next_node_loc.pos, road_height),
                    .uv = {side_width, 0.0f},
                    .object_id = id};
                vertex_buffer.data[vert_idx + 3] = {
                    .pos = height_dim_add(next_node_loc.pos, road_height + building_height),
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
    }

    ///////////////////////////////////////////////////////////////////
    // ~mgj: Create roof
    U32 roof_base_index = base_index_idx;
    {
        prof_scope_marker_named("Roof Creation");
        for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
        {
            osm::Way* way = &ways.data[way_idx];
            Buffer<osm::UtmLocation> buildings_utm_node_buffer =
                BufferAlloc<osm::UtmLocation>(scratch.arena, way->node_count - 1);
            for (U32 idx = 0; idx < way->node_count - 1; idx += 1)
            {
                buildings_utm_node_buffer.data[idx] = city::utm_location_find(way->node_ids[idx]);
            }

            // ~mgj: ignore collinear line segments
            Buffer<osm::UtmLocation> final_utm_node_buffer =
                BufferAlloc<osm::UtmLocation>(scratch.arena, buildings_utm_node_buffer.size);
            {
                U32 cur_idx = 0;
                for (U32 idx = 0; idx < buildings_utm_node_buffer.size; idx += 1)
                {
                    Vec2F32 prev_pos = buildings_utm_node_buffer
                                           .data[(buildings_utm_node_buffer.size + idx - 1) %
                                                 buildings_utm_node_buffer.size]
                                           .pos;
                    Vec2F32 cur_pos =
                        buildings_utm_node_buffer.data[idx % buildings_utm_node_buffer.size].pos;
                    Vec2F32 next_pos =
                        buildings_utm_node_buffer.data[(idx + 1) % buildings_utm_node_buffer.size]
                            .pos;

                    B32 is_collinear = AreTwoConnectedLineSegmentsCollinear(
                        vec_2f64(prev_pos.x, prev_pos.y), vec_2f64(cur_pos.x, cur_pos.y),
                        vec_2f64(next_pos.x, next_pos.y));
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
                osm::UtmLocation node_utm = final_utm_node_buffer.data[idx];
                node_pos_buffer.data[idx] = node_utm.pos;
            }

            Buffer<U32> polygon_index_buffer = EarClipping(scratch.arena, node_pos_buffer);
            if (polygon_index_buffer.size > 0)
            {
                // vertex buffer fill
                for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
                {
                    osm::UtmLocation node_utm = final_utm_node_buffer.data[idx];
                    Vec2U32 id = {.u64 = node_utm.id};
                    vertex_buffer.data[base_vertex_idx + idx] = {
                        .pos = height_dim_add(node_utm.pos, road_height + building_height),
                        .uv = {node_utm.pos.x, node_utm.pos.y},
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
    }

    {
        prof_scope_marker_named("buildings_buffers_create_buffer_copy");
        Buffer<render::Vertex3D> vertex_buffer_final =
            BufferAlloc<render::Vertex3D>(arena, base_vertex_idx);
        Buffer<U32> index_buffer_final = BufferAlloc<U32>(arena, base_index_idx);
        BufferCopy(vertex_buffer_final, vertex_buffer, base_vertex_idx);
        BufferCopy(index_buffer_final, index_buffer, base_index_idx);
        out_render_info->vertex_buffer = vertex_buffer_final;
        out_render_info->index_buffer = index_buffer_final;
    }

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
    F64 total = 0;
    for (U32 idx = 0; idx < node_buffer.size; idx += 1)
    {
        Vec2F32 a = node_buffer.data[idx];
        Vec2F32 b = node_buffer.data[(idx + 1) % node_buffer.size];

        F64 cross_product_z = cross_2f64_z_component(vec_2f64(a.x, a.y), vec_2f64(b.x, b.y));
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

    Vec2F64 p1_f64 = vec_2f64(p1.x, p1.y);
    Vec2F64 p2_f64 = vec_2f64(p2.x, p2.y);
    Vec2F64 p3_f64 = vec_2f64(p3.x, p3.y);
    Vec2F64 point_f64 = vec_2f64(point.x, point.y);

    d1 = cross_2f64_z_component(sub_2f64(point_f64, p1_f64), sub_2f64(p2_f64, p1_f64));
    d2 = cross_2f64_z_component(sub_2f64(point_f64, p2_f64), sub_2f64(p3_f64, p2_f64));
    d3 = cross_2f64_z_component(sub_2f64(point_f64, p3_f64), sub_2f64(p1_f64, p3_f64));

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
    prof_scope_marker;
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
    for (; idx < index_buffer.size;)
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

        F64 cross_product_z = cross_2f64_z_component(vec_2f64(prev_to_ear.x, prev_to_ear.y),
                                                     vec_2f64(ear_to_next.x, ear_to_next.y));

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
                continue;
            }
        }
        else if (cross_product_z == 0)
        {
            DEBUG_LOG("EarClipping: Two line segments are collinear");
        }
        idx++;
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
land_destroy(render::Model3DPipelineDataList list)
{
    for (render::Model3DPipelineDataNode* data = list.first; data; data = data->next)
    {
        render::buffer_destroy(data->handles.index_buffer_handle);
        render::buffer_destroy(data->handles.vertex_buffer_handle);
        render::texture_destroy(data->handles.texture_handle);
    }
}

g_internal render::Model3DPipelineDataList
land_create(Arena* arena, String8 glb_path)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    gltfw_Result glb_data = gltfw_glb_read(arena, glb_path);

    // create render handles
    Buffer<render::Handle> tex_handles =
        BufferAlloc<render::Handle>(scratch.arena, glb_data.textures.size);
    {
        for (U32 i = 0; i < glb_data.textures.size; ++i)
        {
            gltfw_Texture* texture = &glb_data.textures.data[i];
            render::SamplerInfo sampler_info = city::sampler_from_cgltf_sampler(texture->sampler);
            tex_handles.data[i] =
                render::texture_handle_create(&sampler_info, render::PipelineUsageType_3D);
            render::texture_gpu_upload_sync(tex_handles.data[i], texture->tex_buf);
        }
    }

    // create vertex and index buffers
    render::Model3DPipelineDataList handles_list = {};
    {
        for (gltfw_Primitive* primitive = glb_data.primitives.first; primitive;
             primitive = primitive->next)
        {
            Buffer<render::Vertex3D> vertex_buffer =
                vertex_3d_from_gltfw_vertex(arena, primitive->vertices);
            render::BufferInfo vertex_buffer_info =
                render::buffer_info_from_vertex_3d_buffer(vertex_buffer, render::BufferType_Vertex);
            render::BufferInfo index_buffer = render::buffer_info_from_u32_index_buffer(
                primitive->indices, render::BufferType_Index);

            render::Handle vertex_handle = render::buffer_load(&vertex_buffer_info);
            render::Handle index_handle = render::buffer_load(&index_buffer);
            render::Handle texture_handle = tex_handles.data[primitive->tex_idx];

            render::Model3DPipelineDataNode* node =
                PushStruct(arena, render::Model3DPipelineDataNode);
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

g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler)
{
    render::Filter min_filter = render::Filter_Nearest;
    render::Filter mag_filter = render::Filter_Nearest;
    render::MipMapMode mipmap_mode = render::MipMapMode_Nearest;
    render::SamplerAddressMode address_mode_u = render::SamplerAddressMode_Repeat;
    render::SamplerAddressMode address_mode_v = render::SamplerAddressMode_Repeat;

    switch (sampler.min_filter)
    {
        default: break;
        case cgltf_filter_type_nearest:
        {
            min_filter = render::Filter_Nearest;
            mag_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            min_filter = render::Filter_Nearest;
            mag_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            mag_filter = render::Filter_Nearest;
            min_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Linear;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Linear;
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
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_nearest:
            {
                mag_filter = render::Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest_mipmap_linear:
            {
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_linear:
            {
                mag_filter = render::Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest:
            {
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear:
            {
                mag_filter = render::Filter_Linear;
            }
            break;

                break;
        }
    }

    switch (sampler.wrap_s)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_u = render::SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_u = render::SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_u = render::SamplerAddressMode_MirroredRepeat;
            break;
    }
    switch (sampler.wrap_t)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_v = render::SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_v = render::SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_v = render::SamplerAddressMode_MirroredRepeat;
            break;
    }

    render::SamplerInfo sampler_info = {.min_filter = min_filter,
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
    String8 str_copy = push_str8_copy(arena, str);
    return str_copy;
}

g_internal Road*
road_create(String8 texture_path, String8 cache_path, String8 data_dir, Rng2F64 bbox,
            Rng2F64 utm_coords, render::SamplerInfo* sampler_info)
{
    prof_scope_marker;

    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = ArenaAlloc();

    Road* road = PushStruct(arena, Road);

    road->openapi_data_file_name = push_str8_copy(arena, S("openapi_node_ways_highway.json"));
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
        str8_path_from_str8_list(scratch.arena, {cache_path, road->openapi_data_file_name});
    String8 cache_meta_file = PushStr8Cat(scratch.arena, cache_data_file, S(".meta"));

    String8 input_str = str8_from_bbox(scratch.arena, bbox);

    Result<String8> cache_read_result =
        city::cache_read(scratch.arena, cache_data_file, cache_meta_file, input_str);
    String8 http_data = cache_read_result.v;
    if (cache_read_result.err)
    {
        http_data = city::city_http_call_wrapper(scratch.arena, query_str, &params);
        city::cache_write(cache_data_file, cache_meta_file, http_data, input_str);
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
                city::cache_write(cache_data_file, cache_meta_file, http_data, input_str);
                error = false;
            }
        }
    }

    Buffer<osm::RoadNodeList> node_hashmap = json_result.road_nodes;
    osm::structure_add(node_hashmap, http_data, osm::WayType_Road);

    String8 neta_path = str8_path_from_str8_list(scratch.arena, {data_dir, S("netascore.geojson")});
    Map<S64, neta::EdgeList>* edge_map =
        neta::osm_way_to_edges_map_create(scratch.arena, neta_path, utm_coords);
    if (!edge_map)
    {
        exit_with_error("Failed to initialize neta");
    }

    road->edge_structure = city::road_edge_structure_create(road->arena);
    road->road_info_map =
        city::road_info_from_edge_id(road->arena, road->edge_structure.edges, edge_map);

    road->render_buffers = city::road_render_buffers_create(
        road->arena, road->edge_structure.edges, road->default_road_width, road->road_height);

    render::BufferInfo vertex_buffer_info = render::buffer_info_from_vertex_3d_buffer(
        road->render_buffers.vertices, render::BufferType_Vertex);
    render::BufferInfo index_buffer_info = render::buffer_info_from_u32_index_buffer(
        road->render_buffers.indices, render::BufferType_Index);

    road->texture_path =
        str8_path_from_str8_list(road->arena, {texture_path, S("road_texture.ktx2")});

    render::Handle texture_handle =
        render::texture_load_async(sampler_info, road->texture_path, render::PipelineUsageType_3D);
    render::Handle vertex_handle = render::buffer_load(&vertex_buffer_info);
    render::Handle index_handle = render::buffer_load(&index_buffer_info);

    road->handles[road->current_handle_idx] = {vertex_handle, index_handle, texture_handle,
                                               road->render_buffers.indices.size, 0};

    return road;
}

g_internal void
road_vertex_buffer_switch(Road* road, RoadOverlayOption overlay_option)
{
    if (overlay_option != road->overlay_option_cur)
    {
        prof_scope_marker_named("road_vertex_buffer_switch: 1. pass");
        road->overlay_option_cur = overlay_option;

        for (auto& vertex : road->render_buffers.vertices)
        {
            RoadInfo* road_info = map_get(road->road_info_map, (EdgeId)vertex.object_id.u64);
            if (road_info)
            {
                Vec3F32 color = road_overlay_option_colors[(U32)overlay_option];
                vertex.color =
                    vec_4f32(color.x, color.y, color.z, road_info->options[(U32)overlay_option]);
            }
        }
        render::BufferInfo vertex_buffer_info = render::buffer_info_from_vertex_3d_buffer(
            road->render_buffers.vertices, render::BufferType_Vertex);
        render::Handle vertex_handle = render::buffer_load(&vertex_buffer_info);

        render::Model3DPipelineData* current_road_pipeline_data =
            &road->handles[road->current_handle_idx];
        U32 next_handle_idx = (road->current_handle_idx + 1) % ArrayCount(road->handles);
        render::Model3DPipelineData* next_road_pipeline_data = &road->handles[next_handle_idx];

        *next_road_pipeline_data = *current_road_pipeline_data;
        next_road_pipeline_data->vertex_buffer_handle = vertex_handle;

        road->new_vertex_handle_loading = true;
    }

    if (road->new_vertex_handle_loading)
    {
        U32 next_handle_idx = (road->current_handle_idx + 1) % ArrayCount(road->handles);
        if (render::is_resource_loaded(road->handles[next_handle_idx].vertex_buffer_handle))
        {
            prof_scope_marker_named("road_vertex_buffer_switch: 2. pass");
            render::Model3DPipelineData* current_road_pipeline_data =
                &road->handles[road->current_handle_idx];
            render::buffer_destroy_deferred(current_road_pipeline_data->vertex_buffer_handle);
            *current_road_pipeline_data = {};

            road->current_handle_idx = next_handle_idx;
            road->new_vertex_handle_loading = false;
        }
    }
}

g_internal EdgeStructure
road_edge_structure_create(Arena* arena)
{
    prof_scope_marker;
    osm::Network* network = osm::g_network;
    Buffer<osm::Way> way_buf = network->ways_arr[osm::WayType_Road];

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

g_internal Map<EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, Buffer<RoadEdge> road_edge_buf,
                       Map<S64, neta::EdgeList>* neta_edge_map)
{
    prof_scope_marker;
    Map<EdgeId, RoadInfo>* road_info_map = map_create<EdgeId, RoadInfo>(arena, 1024);

    for (RoadEdge& edge : road_edge_buf)
    {
        neta::Edge* neta_edge = edge_from_road_edge(&edge, neta_edge_map);
        if (neta_edge)
        {
            RoadInfo info = {};
            info.options[(U32)RoadOverlayOption::Bikeability_ft] = neta_edge->index_bike_ft;
            info.options[(U32)RoadOverlayOption::Bikeability_tf] = neta_edge->index_bike_tf;
            info.options[(U32)RoadOverlayOption::Walkability_ft] = neta_edge->index_walk_ft;
            info.options[(U32)RoadOverlayOption::Walkability_tf] = neta_edge->index_walk_tf;
            map_insert(road_info_map, edge.id, info);
        }
    }

    return road_info_map;
}

g_internal neta::Edge*
edge_from_road_edge(RoadEdge* road_edge, Map<osm::WayId, neta::EdgeList>* edge_list_map)
{
    S64 from_id = road_edge->node_id_from;
    S64 to_id = road_edge->node_id_to;

    osm::UtmLocation from_node_loc = city::utm_location_find(from_id);
    osm::UtmLocation to_node_loc = city::utm_location_find(to_id);
    Vec2F64 from_node_coord = vec_2f64(from_node_loc.pos.x, from_node_loc.pos.y);
    Vec2F64 to_node_coord = vec_2f64(to_node_loc.pos.x, to_node_loc.pos.y);

    S64 way_id = road_edge->way_id;
    neta::EdgeList* edge_list = map_get(edge_list_map, way_id);

    neta::Edge* chosen_edge = {};
    if (edge_list)
    {
        F64 smallest_dist = max_f64;
        for (neta::EdgeNode* edge_node = edge_list->first; edge_node; edge_node = edge_node->next)
        {
            neta::Edge* edge = edge_node->edge;
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
    }
    return chosen_edge;
}

g_internal Buffer<render::Vertex3D>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer)
{
    Buffer<render::Vertex3D> out_vertex_buffer =
        BufferAlloc<render::Vertex3D>(arena, in_vertex_buffer.size);
    for (U32 i = 0; i < in_vertex_buffer.size; i++)
    {
        out_vertex_buffer.data[i].pos = in_vertex_buffer.data[i].pos;
        out_vertex_buffer.data[i].uv = in_vertex_buffer.data[i].uv;
    }
    return out_vertex_buffer;
}

} // namespace city
