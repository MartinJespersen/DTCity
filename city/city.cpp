namespace city
{
static void
CityInit(wrapper::VulkanContext* vk_ctx, City* city, String8 cwd)
{
    city->arena = ArenaAlloc();
    city->road.node_slot_count = 100;

    city->w_road = PushStruct(city->arena, wrapper::Road);
    city->road.road_height = 10.0f;
    city->road.road_width = 3.0f;

    wrapper::RoadInit(vk_ctx, city, cwd);
}

static void
CityUpdate(City* city, wrapper::VulkanContext* vk_ctx, U32 image_index)
{
    // ~mgj: road building update
    wrapper::Road* w_road = city->w_road;
    Road* road = &city->road;
    wrapper::RoadUpdate(road, w_road, vk_ctx, image_index);
};

static void
CityCleanup(City* city, wrapper::VulkanContext* vk_ctx)
{
    wrapper::RoadCleanup(city, vk_ctx);
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

static RoadQuadCoord
RoadQuadUtmFromQgisRoadSegment(F64 lat_bot, F64 lon_bot, F64 lat_top, F64 lon_top,
                               F64 center_transform_x, F64 center_transform_y, F32 road_half_width)
{
    F64 road_node_0_x;
    F64 road_node_0_y;
    F64 road_node_1_x;
    F64 road_node_1_y;

    char UTMZone[10];
    UTM::LLtoUTM(lat_bot, lon_bot, road_node_0_y, road_node_0_x, UTMZone);
    UTM::LLtoUTM(lat_top, lon_top, road_node_1_y, road_node_1_x, UTMZone);

    glm::vec2 road_0_pos =
        glm::vec2(road_node_0_x + center_transform_x, road_node_0_y + center_transform_y);
    glm::vec2 road_1_pos =
        glm::vec2(road_node_1_x + center_transform_x, road_node_1_y + center_transform_y);

    glm::vec2 road_dir = road_0_pos - road_1_pos;
    glm::vec2 orthogonal_vec = {road_dir.y, -road_dir.x};
    glm::vec2 normal = glm::normalize(orthogonal_vec);
    glm::vec2 normal_scaled = normal * road_half_width;

    RoadQuadCoord road_quad_coord;
    road_quad_coord.pos[0] = road_0_pos + normal_scaled;
    road_quad_coord.pos[1] = road_0_pos + (-normal_scaled);
    road_quad_coord.pos[2] = road_1_pos + normal_scaled;
    road_quad_coord.pos[3] = road_1_pos + (-normal_scaled);
    return road_quad_coord;
}

static RoadQuadCoord
RoadSegmentFromNodeIds(Road* road, RoadWay* way, U32 index_0, U32 index_1, F64 center_transform_x,
                       F64 center_transform_y, F32 road_half_width)
{
    Assert(index_0 < way->node_count);
    Assert(index_1 < way->node_count);

    U64 roadseg0_node_id_0 = way->node_ids[index_0];
    U64 roadseg0_node_id_1 = way->node_ids[index_1];
    RoadNode* road_node_0 = NodeFind(road, roadseg0_node_id_0);
    RoadNode* road_node_1 = NodeFind(road, roadseg0_node_id_1);
    RoadQuadCoord road_quad_coords = RoadQuadUtmFromQgisRoadSegment(
        road_node_0->lat, road_node_0->lon, road_node_1->lat, road_node_1->lon, center_transform_x,
        center_transform_y, road_half_width);
    return road_quad_coords;
}

static void
RoadsBuild(Arena* arena, City* city)
{
    Road* road = &city->road;
    ScratchScope scratch = ScratchScope(&arena, 1);

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

    F64 meter_diff_x = out_easting_high - out_easting_low;
    F64 meter_diff_y = out_northing_high - out_northing_low;
    F64 lon_diff = lon_high - lon_low;
    F64 lat_diff = lat_high - lat_low;
    F64 x_scale = meter_diff_x / lon_diff;
    F64 y_scale = meter_diff_y / lat_diff;

    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;
    params.content_type = Str8CString("text/html");

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

    String8 host = Str8CString("http://overpass-api.de");
    String8 path = Str8CString("/api/interpreter");
    HTTP_Response response = HTTP_Request(arena, host, path, query_str, &params);

    String8 content = Str8((U8*)response.body.str, response.body.size);

    wrapper::OverpassHighways(scratch.arena, road, content);

    F32 road_half_width = road->road_width / 2.0f; // Example value, adjust as needed
    F64 long_low_utm;
    F64 lat_low_utm;
    F64 long_high_utm;
    F64 lat_high_utm;
    char utm_zone[10];
    UTM::LLtoUTM(lat_low, lon_low, lat_low_utm, long_low_utm, utm_zone);
    UTM::LLtoUTM(lat_high, lon_high, lat_high_utm, long_high_utm, utm_zone);
    F64 center_transform_x = -(long_low_utm + long_high_utm) / 2.0;
    F64 center_transform_y = -(lat_low_utm + lat_high_utm) / 2.0;

    // road->way_count = 2;
    // ~mgj: Calculate node
    U64 node_count = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];
        // + 1 is the result of adding two number:
        // -1 get the number of road segments (because the first and last vertices are shared
        // between segments)
        node_count += way->node_count - 1;
    }
    // the addition of road->way_count * 2 is due to the triangle strip topology used to render the
    // road segments
    U64 total_vert_count = node_count * 4 + road->way_count * 2;

    road->vertex_buffer =
        BufferAlloc<RoadVertex>(city->arena, total_vert_count); // 4 vertices per line segment

    U32 road_segment_count = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];

        // TODO: what if way has less than two nodes?
        // first road segment

        RoadQuadCoord road_quad_coords_first = RoadSegmentFromNodeIds(
            road, way, 0, 1, center_transform_x, center_transform_y, road_half_width);
        // last road segment
        U32 second_to_last_node_index = way->node_count - 2;
        U32 last_node_index = way->node_count - 1;
        RoadQuadCoord road_quad_coords_last =
            RoadSegmentFromNodeIds(road, way, second_to_last_node_index, last_node_index,
                                   center_transform_x, center_transform_y, road_half_width);

        U64 current_vertex_index = road_segment_count * 4 + way_index * 2;
        road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_first.pos[0];
        // for each road segment (road segment = two connected nodes)
        for (U32 node_index = 1; node_index < way->node_count; node_index++)
        {
            RoadQuadCoord road_quad_coords =
                RoadSegmentFromNodeIds(road, way, node_index - 1, node_index, center_transform_x,
                                       center_transform_y, road_half_width);

            // +1 due to duplicate of first and last way node to seperate roadways in triangle strip
            // topology
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[0];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[1];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[2];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[3];
        }
        road->vertex_buffer.data[current_vertex_index].pos = road_quad_coords_last.pos[3];
        road_segment_count += way->node_count - 1;
    }
}
} // namespace city
