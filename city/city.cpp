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
    road->default_road_width = 3.0f;
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
            node_utm->x_utm = x + road->utm_center_offset.x;
            node_utm->y_utm = y + road->utm_center_offset.y;
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

    F64 meter_diff_x = out_easting_high - out_easting_low;
    F64 meter_diff_y = out_northing_high - out_northing_low;
    F64 lon_diff = lon_high - lon_low;
    F64 lat_diff = lat_high - lat_low;
    F64 x_scale = meter_diff_x / lon_diff;
    F64 y_scale = meter_diff_y / lat_diff;

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
    String8 content;
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
            printf("RoadsBuild: Could not read everything from cache");
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

    // road->way_count = 2;
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
    U64 total_vert_count = node_count * 7 + road->way_count * 2;

    road->vertex_buffer =
        BufferAlloc<RoadVertex>(road->arena, total_vert_count); // 4 vertices per line segment

    U32 road_segment_count = 0;
    U32 current_vertex_index = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];

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
        F32 road_half_width = road_width / 2.0f;

        // TODO: what if way has less than two nodes?
        //
        // first road segment
        RoadQuadCoord road_quad_coords_first = RoadSegmentFromNodeIds(
            road, way, 0, 1, center_transform_x, center_transform_y, road_half_width);
        // last road segment
        U32 second_to_last_node_index = way->node_count - 2;
        U32 last_node_index = way->node_count - 1;
        RoadQuadCoord road_quad_coords_last =
            RoadSegmentFromNodeIds(road, way, second_to_last_node_index, last_node_index,
                                   center_transform_x, center_transform_y, road_half_width);

        road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_first.pos[0];
        // for each road segment (road segment = two connected nodes)

        if (way->node_count < 2)
        {
            exitWithError("expected at least one road segment comprising of two nodes");
        }

        RoadQuadCoord road_quad_coords;
        RoadQuadCoord road_quad_coords_next;
        for (U32 node_index = 1; node_index < way->node_count; node_index++)
        {
            if (node_index == 1)
            {
                road_quad_coords =
                    RoadSegmentFromNodeIds(road, way, node_index - 1, node_index,
                                           center_transform_x, center_transform_y, road_half_width);
            }

            F32 y_len = road_quad_coords.pos[1].y - road_quad_coords.pos[0].y;
            F32 x_len = road_quad_coords.pos[2].x - road_quad_coords.pos[0].x;

            // +1 due to duplicate of first and last way node to seperate roadways in triangle strip
            // topology
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[0];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[1];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[2];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[3];

            if (node_index < way->node_count - 1)
            {
                road_quad_coords_next =
                    RoadSegmentFromNodeIds(road, way, node_index, node_index + 1,
                                           center_transform_x, center_transform_y, road_half_width);

                F32 x_len_conn = road_quad_coords_next.pos[2].x - road_quad_coords.pos[0].x;

                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_next.pos[1];
                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[2];
                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[3];

                road_quad_coords = road_quad_coords_next;
            }
        }
        road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_last.pos[3];
        road_segment_count += way->node_count - 1;
    }

    // ~mgj: Road Decision tree create
    road->node_hashmap = RoadNodeStructureCreate(road);
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

    car_sim->car = wrapper::CarCreate(vk_ctx, parsed_result.sampler, car_sim->vertex_buffer,
                                      car_sim->index_buffer);
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
            glm::vec3(source_node->x_utm, road->road_height - car_sim->car_center_offset.min,
                      source_node->y_utm);
        car->dir = glm::normalize(glm::vec3(target_node->x_utm - source_node->x_utm, 0,
                                            target_node->y_utm - source_node->y_utm));
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
            glm::vec3(car_info->target->x_utm, car_info->cur_pos.y, car_info->target->y_utm);
        glm::vec3 new_pos = car_info->cur_pos + car_info->dir * car_speed_default * time_delta;

        // Is the destination point in between new and old pos?
        F32 min_x = Min(car_info->cur_pos.x, new_pos.x);
        F32 max_x = Max(car_info->cur_pos.x, new_pos.x);
        F32 min_y = Min(car_info->cur_pos.y, new_pos.y);
        F32 max_y = Max(car_info->cur_pos.y, new_pos.y);

        // Check if the car has reached its destination. If so, find new destination and direction.
        if ((target_pos.x >= min_x && target_pos.x <= max_x) &&
            (target_pos.y >= min_y && target_pos.y <= max_y))
        {
            NodeUtm* new_target = NeighbourNodeChoose(car_info->target, road);
            glm::vec3 new_target_pos =
                glm::vec3(new_target->x_utm, car_info->cur_pos.y, new_target->y_utm);
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
