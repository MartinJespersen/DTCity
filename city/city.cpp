namespace city
{
static void
CityInit(City* city, String8 cwd)
{
    city->arena = ArenaAlloc();
    city->road.node_slot_count = 100;
    city->road_width = 10;
    wrapper::RoadPipelineCreate(city, cwd);
}

static void
CityCleanup(City* city)
{
    wrapper::RoadCleanup(city);
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
RoadsBuild(Arena* arena, City* city)
{
    F32 road_half_width = city->road_width;
    F32 road_height = city->road_height;
    Road* road = &city->road;
    ScratchScope scratch = ScratchScope(&arena, 1);

    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;

    const char* query = R"(data=
        [out:json] [timeout:25];
        (
          way["highway"](56.16923976826141, 10.1852768812041, 56.17371342689877, 10.198376789774187);
        );
        out body;
        >;
        out skel qt;
    )";

    HTTP_Response response = HTTP_Request(
        arena, Str8CString("https://overpass-api.de/api/interpreter"), Str8CString(query), &params);

    String8 content = Str8((U8*)response.body.str, response.body.size);

    wrapper::OverpassHighways(scratch.arena, road, content);

    RoadWay* way = &road->ways[0];
    U64 node_count = way->node_count;

    U64 vert_count = node_count + 2; // +2 as we use a line adjacency strip topology
    road->vertices = BufferAlloc<RoadVertex>(city->arena, vert_count);

    // ~mgj: padding for line adjacency strip topology
    U64 node_id_start = way->node_ids[0];
    U64 node_id_end = way->node_ids[vert_count - 1];
    RoadNode* road_node_start = NodeFind(road, node_id_start);
    RoadNode* road_node_end = NodeFind(road, node_id_end);
    road->vertices.data[0] = {{road_node_start->lon, road_node_start->lat}, road_node_start->id};
    road->vertices.data[vert_count - 1] = {{road_node_end->lon, road_node_end->lat},
                                           road_node_end->id};

    U32 quad_counter = 0;
    for (int i = 0; i < way->node_count - 1; i++)
    {
        U64 node_id = way->node_ids[i];
        RoadNode* road_node = NodeFind(road, node_id);
        road->vertices.data[i] = {{road_node->lon, road_node->lat}, road_node->id};
    }
}
// struct QuadCoord
// {
//     QuadCoord* next;
//     Vertex lt;
//     Vertex lb;
//     Vertex rt;
//     Vertex rb;
// };
// U64 vert_count = node_count + 2; // +2 as we use a line adjacency strip topology
// road->vertices = BufferAlloc<RoadVertex>(city->arena, vert_count);

// struct QuadCoordList
// {
//     QuadCoord* first;
//     QuadCoord* last;
// };
// // ~mgj: padding for line adjacency strip topology
// U64 node_id_start = way->node_ids[0];
// U64 node_id_end = way->node_ids[vert_count - 1];
// RoadNode* road_node_start = NodeFind(road, node_id_start);
// RoadNode* road_node_end = NodeFind(road, node_id_end);
// road->vertices.data[0] = {{road_node_start->lon, road_node_start->lat}, road_node_start->id};
// road->vertices.data[vert_count - 1] = {{road_node_end->lon, road_node_end->lat},
// road_node_end->id};

// QuadCoordList* quad_list = PushStruct(scratch.arena, QuadCoordList);
// U32 quad_counter = 0;
// for (int i = 0; i < way->node_count - 1; i++)
// {
//     U64 node_id_0 = way->node_ids[i];
//     U64 node_id_1 = way->node_ids[i + 1];

//     RoadNode* road_node_0 = NodeFind(road, node_id_0);
//     RoadNode* road_node_1 = NodeFind(road, node_id_1);

//     Vec2F32 road_0_pos = {road_node_0->lon, road_node_0->lat};
//     Vec2F32 road_1_pos = {road_node_1->lon, road_node_1->lat};

//     Vec2F32 road_dir = Sub2F32(road_0_pos, road_1_pos);
//     Vec2F32 orthogonal_vec = {road_dir.y, -road_dir.x};
//     Vec2F32 normal = Normalize2F32(orthogonal_vec);
//     Vec2F32 normal_scaled = Scale2F32(normal, road_half_width);

//     Vec2F32 vert_lt = Add2F32(road_0_pos, normal_scaled);
//     Vec2F32 vert_lb = Add2F32(road_0_pos, Scale2F32(normal_scaled, -1.0f));
//     Vec2F32 vert_rt = Add2F32(road_1_pos, normal_scaled);
//     Vec2F32 vert_rb = Add2F32(road_1_pos, Scale2F32(normal_scaled, -1.0f));

//     QuadCoord* quad = PushStruct(scratch.arena, QuadCoord);
//     quad->lt = {{vert_lt.x, road_height, vert_lt.y}};
//     quad->lb = {{vert_lb.x, road_height, vert_lb.y}};
//     quad->rt = {{vert_rt.x, road_height, vert_rt.y}};
//     quad->rb = {{vert_rb.x, road_height, vert_rb.y}};
//     SLLQueuePush(quad_list->first, quad_list->last, quad);
//     quad_counter++;
// }

// road->vertices = BufferAlloc<Vertex>(city->arena, 4 * quad_counter);
// road->indices = BufferAlloc<U32>(city->arena, 6 * quad_counter);

// if (quad_counter > 0)
// {
//     quad_counter = 0;
//     for (QuadCoord* quad = quad_list->first; quad != NULL; quad = quad->next)
//     {
//         U32 vert_index = quad_counter * 4;
//         U32 lt_index = vert_index + 0;
//         U32 lb_index = vert_index + 1;
//         U32 rt_index = vert_index + 2;
//         U32 rb_index = vert_index + 3;

//         road->vertices.data[lt_index] = quad->lt;
//         road->vertices.data[lt_index] = quad->lb;
//         road->vertices.data[rt_index] = quad->rt;
//         road->vertices.data[rb_index] = quad->rb;
//         road->indices.data[quad_counter * 6 + 0] = lt_index;
//         road->indices.data[quad_counter * 6 + 1] = lb_index;
//         road->indices.data[quad_counter * 6 + 2] = rt_index;
//         road->indices.data[quad_counter * 6 + 3] = lb_index;
//         road->indices.data[quad_counter * 6 + 4] = rb_index;
//         road->indices.data[quad_counter * 6 + 5] = rt_index;
//         quad_counter++;
//     }
//     U64 node_id = way->node_ids[i];
//     RoadNode* road_node = NodeFind(road, node_id);
//     road->vertices.data[i] = {{road_node->lon, road_node->lat}, road_node->id};
// }
// }
} // namespace city
