namespace city
{
static Road*
RoadCreate(wrapper::VulkanContext* vk_ctx, String8 cache_path)
{
    Arena* arena = ArenaAlloc();

    Road* road = PushStruct(arena, Road);

    road->openapi_data_cache_path =
        Str8PathFromStr8List(arena, {cache_path, S("openapi_data.txt")});
    road->arena = arena;
    road->node_slot_count = 100;
    road->w_road = wrapper::RoadCreate(vk_ctx, road);
    road->road_height = 10.0f;
    road->default_road_width = 2.0f;
    road->node_hashmap_size = 100;

    return road;
}

static void
RoadDestroy(wrapper::VulkanContext* vk_ctx, Road* road)
{
    wrapper::RoadDestroy(road, vk_ctx);
    ArenaRelease(road->arena);
}

static inline RoadNode*
NodeFind(Road* road, U64 node_id)
{
    RoadNodeSlot node_slot = road->nodes[node_id % road->node_slot_count];

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

static RoadTagResult
RoadTagFind(Arena* arena, Buffer<RoadTag> tags, String8 tag_to_find)
{
    RoadTagResult result = {};
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
UniqueNodeAndWayInsert(Arena* arena, U64 node_id, RoadWay* road_way, Buffer<NodeUtmSlot> hashmap,
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

    RoadWayListElement* road_way_element = PushStruct(arena, RoadWayListElement);
    road_way_element->road_way = road_way;

    SLLQueuePush(node->roadway_queue.first, node->roadway_queue.last, road_way_element);

    *out = node;
    return node_inserted;
}

static Buffer<NodeUtmSlot>
RoadNodeStructureCreate(Road* road)
{
    Buffer<NodeUtmSlot> nodes = BufferAlloc<NodeUtmSlot>(road->arena, road->node_hashmap_size);
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];
        for (U32 node_index = 0; node_index < way->node_count; node_index++)
        {
            U64 node_id = way->node_ids[node_index];
            NodeUtm* node_utm;
            UniqueNodeAndWayInsert(road->arena, node_id, way, nodes, &node_utm);
            RoadNode* node_coord = NodeFind(road, node_id);
            char utm_zone[10];
            double x, y;
            UTM::LLtoUTM(node_coord->lat, node_coord->lon, y, x, utm_zone);

            String8 utm_zone_str = Str8CString(utm_zone);
            node_utm->utm_zone = PushStr8Copy(road->arena, utm_zone_str);
            node_utm->pos.x = x + road->utm_center_offset.x;
            node_utm->pos.y = y + road->utm_center_offset.y;
        }
    }

    return nodes;
}

static void
RoadsBuild(Road* road)
{
    ScratchScope scratch = ScratchScope(0, 0);

    // TODO: make this an input and check for input conditions
    F64 lat_low = 56.16923976826141;
    F64 lon_low = 10.1852768812041;
    F64 lat_high = 56.17371342689877;
    F64 lon_high = 10.198376789774187;

    F64 out_northing_low = 0;
    F64 out_easting_low = 0;
    char zone_low[265];
    UTM::LLtoUTM(lat_low, lon_low, out_easting_low, out_northing_low, zone_low);
    F64 out_northing_high = 0;
    F64 out_easting_high = 0;
    char zone_high[265];
    UTM::LLtoUTM(lat_high, lon_high, out_easting_high, out_northing_high, zone_high);

    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;
    params.content_type = S("text/html");

    const char* query = R"(data=
        [out:json] [timeout:25];
        (
          way["highway"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )";
    String8 query_str =
        PushStr8F(scratch.arena, (char*)query, lat_low, lon_low, lat_high, lon_high);

    B32 read_from_cache = 0;
    String8 content = {0};
    if (OS_FilePathExists(road->openapi_data_cache_path))
    {
        OS_Handle file_handle = OS_FileOpen(OS_AccessFlag_Read, road->openapi_data_cache_path);
        FileProperties file_props = OS_PropertiesFromFile(file_handle);

        content.str = PushArray(scratch.arena, U8, file_props.size);
        content.size = file_props.size;
        U64 total_read_size =
            OS_FileRead(file_handle, {.min = 0, .max = file_props.size}, content.str);
        if (total_read_size != file_props.size)
        {
            DEBUG_LOG("RoadsBuild: Could not read everything from cache");
        }
        else
        {
            read_from_cache = 1;
        }
        OS_FileClose(file_handle);
    }

    if (!read_from_cache)
    {
        String8 host = S("https://overpass-api.de");
        String8 path = S("/api/interpreter");
        HTTP_Response response = HTTP_Request(scratch.arena, host, path, query_str, &params);

        if (!response.good)
        {
            exitWithError(
                "RoadsBuild: http request did not succeed and Road information is not available");
        }
        content = Str8((U8*)response.body.str, response.body.size);

        OS_Handle file_write_handle =
            OS_FileOpen(OS_AccessFlag_Write, road->openapi_data_cache_path);
        U64 bytes_written =
            OS_FileWrite(file_write_handle, {.min = 0, .max = content.size}, content.str);
        if (bytes_written != content.size)
        {
            exitWithError("RoadsBuild: Was not able to write all openapi data to cache");
        }
        OS_FileClose(file_write_handle);
    }

    wrapper::OverpassHighways(road, content);

    F64 long_low_utm;
    F64 lat_low_utm;
    F64 long_high_utm;
    F64 lat_high_utm;
    char utm_zone[10];
    UTM::LLtoUTM(lat_low, lon_low, lat_low_utm, long_low_utm, utm_zone);
    UTM::LLtoUTM(lat_high, lon_high, lat_high_utm, long_high_utm, utm_zone);
    F64 center_transform_x = -(long_low_utm + long_high_utm) / 2.0;
    F64 center_transform_y = -(lat_low_utm + lat_high_utm) / 2.0;
    road->utm_center_offset = {center_transform_x, center_transform_y};

    // ~mgj: Road Decision tree create
    road->node_hashmap = RoadNodeStructureCreate(road);

    // road->way_count = 2;
    U64 total_road_segment_count = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];
        // + 1 is the result of adding two number:
        // -1 get the number of road segments (because the first and last vertices are shared
        // between segments)
        total_road_segment_count += way->node_count - 1;

        // ~mgj: road width calculation
        F32 road_width = road->default_road_width; // Example value, adjust as needed
        {
            Buffer<RoadTag> tags = {.data = way->tags, .size = way->tag_count};
            RoadTagResult result = RoadTagFind(scratch.arena, tags, S("width"));
            if (result.result == RoadTagResultEnum::ROAD_TAG_FOUND)
            {
                F32 float_result = {0};
                if (F32FromStr8(result.value, &float_result))
                {
                    road_width = float_result;
                }
            }
        }
        way->road_width = road_width;
        Assert(road_width > 0.0f);
    }

    // the addition of road->way_count * 2 is due to the triangle strip topology used to render the
    // road segments
    U64 total_vert_count = total_road_segment_count * 12;

    road->vertex_buffer =
        BufferAlloc<RoadVertex>(road->arena, total_vert_count); // 4 vertices per line segment

    U32 current_vertex_index = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];
        // for each road segment (road segment = two connected nodes)

        if (way->node_count < 2)
        {
            exitWithError("expected at least one road segment comprising of two nodes");
        }

        U64 node_first_id = way->node_ids[0];
        U64 node_second_id = way->node_ids[1];

        NodeUtm* node_first = NodeUtmFind(road, node_first_id);
        NodeUtm* node_second = NodeUtmFind(road, node_second_id);

        RoadSegment road_segment_prev;
        RoadSegmentFromTwoRoadNodes(&road_segment_prev, node_first, node_second, way->road_width);

        if (way->node_count == 2)
        {
            RoadIntersectionPointsFind(road, &road_segment_prev, way);
            QuadToBufferAdd(&road_segment_prev, road->vertex_buffer, &current_vertex_index);
        }
        else
        {
            for (U32 node_idx = 1; node_idx < way->node_count - 1; node_idx++)
            {
                U64 node1_id = way->node_ids[node_idx];
                U64 node2_id = way->node_ids[node_idx + 1];

                NodeUtm* node1 = NodeUtmFind(road, node1_id);
                NodeUtm* node2 = NodeUtmFind(road, node2_id);

                RoadSegment road_segment_cur;
                RoadSegmentFromTwoRoadNodes(&road_segment_cur, node1, node2, way->road_width);

                RoadSegmentConnectionFromTwoRoadSegments(&road_segment_prev, &road_segment_cur,
                                                         way->road_width);

                RoadIntersectionPointsFind(road, &road_segment_prev, way);
                QuadToBufferAdd(&road_segment_prev, road->vertex_buffer, &current_vertex_index);
                if (node_idx == way->node_count - 2)
                {
                    RoadIntersectionPointsFind(road, &road_segment_cur, way);
                    QuadToBufferAdd(&road_segment_cur, road->vertex_buffer, &current_vertex_index);
                }

                road_segment_prev = road_segment_cur;
                // TODO: what if road segment is consist of only one or two nodes
            }
        }
    }
}

static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<RoadVertex> buffer, U32* cur_idx)
{
    F32 road_width = Dist2F32(road_segment->start.top, road_segment->start.btm);
    F32 top_tex_scaled = Dist2F32(road_segment->start.top, road_segment->end.top) / road_width;
    F32 btm_tex_scaled = Dist2F32(road_segment->start.btm, road_segment->end.btm) / road_width;

    const F32 uv_x_top = 1;
    const F32 uv_x_btm = 0;
    const F32 uv_y_start = 0.5f - (F32)btm_tex_scaled;
    const F32 uv_y_end = 0.5f + (F32)top_tex_scaled;

    // first triangle
    buffer.data[(*cur_idx)++] = {.pos = road_segment->start.top, .uv = {uv_x_top, uv_y_start}};
    buffer.data[(*cur_idx)++] = {.pos = road_segment->start.btm, .uv = {uv_x_btm, uv_y_start}};
    buffer.data[(*cur_idx)++] = {.pos = road_segment->end.top, .uv = {uv_x_top, uv_y_end}};

    // second triangle
    buffer.data[(*cur_idx)++] = {.pos = road_segment->start.btm, .uv = {uv_x_btm, uv_y_start}};
    buffer.data[(*cur_idx)++] = {.pos = road_segment->end.top, .uv = {uv_x_top, uv_y_end}};
    buffer.data[(*cur_idx)++] = {.pos = road_segment->end.btm, .uv = {uv_x_btm, uv_y_end}};
}

static B32
RoadNodeIsCrossing(NodeUtm* node)
{
    U32 way_count = 0;
    U32 is_part_of_crossing = 0;
    for (RoadWay* way = 0; node->roadway_queue.first; way = way->next)
    {
        way_count++;
    }
    if (way_count > 1)
    {
        is_part_of_crossing = 1;
    }
    return is_part_of_crossing;
}

static NodeUtm*
NodeUtmFind(Road* road, U64 node_id)
{
    U64 node_index = node_id % road->node_hashmap.size;
    NodeUtmSlot* slot = &road->node_hashmap.data[node_index];
    NodeUtm* node = slot->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
            return node;
    }
    return &g_road_node_utm;
}

static void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, RoadWay* current_road_way)
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

        for (RoadWayListElement* road_way_list = node->roadway_queue.first; road_way_list;
             road_way_list = road_way_list->next)
        {
            RoadWay* road_way = road_way_list->road_way;
            if (road_way->id != current_road_way->id)
            {
                // find adjacent nodes on crossing road
                AdjacentNodeLL* adj_node_ll = {};
                for (U32 node_idx = 0; node_idx < road_way->node_count; node_idx++)
                {
                    U64 node_id = road_way->node_ids[node_idx];
                    if (node_id == node->id)
                    {
                        if (node_idx > 0)
                        {
                            U32 prev_node_idx = node_idx - 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 prev_node_id = road_way->node_ids[prev_node_idx];
                            adj_node->node = NodeUtmFind(road, prev_node_id);
                            SLLStackPush(adj_node_ll, adj_node);
                        }

                        if (node_idx < road_way->node_count - 1)
                        {
                            U32 next_node_idx = node_idx + 1;
                            AdjacentNodeLL* adj_node = PushStruct(scratch.arena, AdjacentNodeLL);
                            U64 next_node_id = road_way->node_ids[next_node_idx];
                            adj_node->node = NodeUtmFind(road, next_node_id);
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
                                                road_way->road_width);
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
RoadRandomNodeUtmFind(Road* road)
{
    U32 rand_num = RandomU32();
    for (U32 i = 0; i < road->node_hashmap.size; ++i)
    {
        U32 slot_index = rand_num % road->node_hashmap.size;
        NodeUtmSlot* slot = &road->node_hashmap.data[slot_index];
        if (slot->first != NULL)
        {
            return slot->first;
        }
        rand_num++;
    }
    return &g_road_node_utm;
}

static NodeUtm*
FindUtmNode(Road* road, U64 node_id)
{
    U32 slot_index = node_id % road->node_hashmap.size;
    NodeUtmSlot* slot = &road->node_hashmap.data[slot_index];
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
    for (RoadWayListElement* way_element = node->roadway_queue.first; way_element;
         way_element = way_element->next)
    {
        roadway_count++;
    }
    RoadWayListElement* way_element = node->roadway_queue.first;

    // Find random roadway
    U32 rand_num = RandomU32();
    U32 rand_roadway_idx = rand_num % roadway_count;
    for (U32 i = 0; i < rand_roadway_idx; ++i)
    {
        way_element = way_element->next;
    }
    Assert(way_element);

    RoadWay* way = way_element->road_way;
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
    NodeUtm* next_node = FindUtmNode(road, next_node_id);
    return next_node;
}

// ~mgj: Cars

static Rng1F32
CarCenterHeightOffset(Buffer<CarVertex> vertices)
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
CarSimCreate(wrapper::VulkanContext* vk_ctx, U32 car_count, Road* road)
{
    Arena* arena = ArenaAlloc();

    CarSim* car_sim = PushStruct(arena, CarSim);
    car_sim->arena = arena;

    // parse gltf file
    String8 gltf_path = S("../../../assets/cars/scene.gltf");
    wrapper::CgltfResult parsed_result = wrapper::CgltfParse(arena, gltf_path);
    car_sim->vertex_buffer = parsed_result.vertex_buffer;
    car_sim->index_buffer = parsed_result.index_buffer;
    car_sim->sampler = parsed_result.sampler;

    String8 car_texture_path = PushStr8Copy(arena, S("../../../textures/car_collection.ktx"));
    wrapper::AssetId car_texture_id = wrapper::AssetIdFromStr8(car_texture_path);

    car_sim->car = wrapper::CarCreate(car_texture_id, car_texture_path, parsed_result.sampler,
                                      car_sim->vertex_buffer, car_sim->index_buffer);
    car_sim->car_center_offset = CarCenterHeightOffset(car_sim->vertex_buffer);

    // create cars for simulation
    car_sim->cars = BufferAlloc<Car>(arena, car_count);
    // initialize cars

    for (U32 i = 0; i < car_count; ++i)
    {
        NodeUtm* source_node = RoadRandomNodeUtmFind(road);
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
CarSimDestroy(wrapper::VulkanContext* vk_ctx, CarSim* car_sim)
{
    wrapper::CarDestroy(vk_ctx, car_sim->car);
    ArenaRelease(car_sim->arena);
}

static Buffer<CarInstance>
CarUpdate(Arena* arena, CarSim* car, Road* road, F32 time_delta)
{
    Buffer<CarInstance> instance_buffer = BufferAlloc<CarInstance>(arena, car->cars.size);
    CarInstance* instance;
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

} // namespace city
