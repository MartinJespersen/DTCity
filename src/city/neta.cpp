static Result<Buffer<neta_Edge>>
neta_edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox)
{
    using namespace simdjson;

    B32 err = false;
    ChunkList<neta_Edge>* way_local_list = chunk_list_create<neta_Edge>(arena, 7);

    fallback::ondemand::array feature_array;
    err |= doc.get_object()["features"].get_array().get(feature_array);
    for (auto elem : feature_array)
    {
        auto props = elem["properties"];
        S64 osm_id = 0;
        err |= props["osm_id"].get_int64().get(osm_id);

        osm::WayNode* osm_way = osm::way_find(osm_id);
        if (!osm_way) // do not save road ways not in the osm database
            continue;

        // insert into buffer to hold neta information for each edge/way
        neta_Edge* way_local = chunk_list_get_next<neta_Edge>(arena, way_local_list);
        way_local->osm_id = osm_id;
        err |= props["edge_id"].get_int64().get(way_local->edge_id);
        err |= props["index_bike_ft"].get_double().get(way_local->index_bike_ft);
        err |= props["index_bike_tf"].get_double().get(way_local->index_bike_tf);
        err |= props["index_walk_ft"].get_double().get(way_local->index_walk_ft);
        err |= props["index_walk_tf"].get_double().get(way_local->index_walk_tf);

        if (err & ~simdjson::error_code::INCORRECT_TYPE)
            break;

        err = 0;
        auto geometry = elem["geometry"]["coordinates"].get_array();
        if (geometry.error() != simdjson::error_code::SUCCESS)
        {
            err |= geometry.error();
            break;
        }
        U64 size = geometry.count_elements();
        Buffer<Vec2F64> coords_buf = BufferAlloc<Vec2F64>(arena, size);
        U64 coord_idx = 0;
        for (auto point : geometry)
        {
            auto coords = point.get_array();
            if (coords.error() != simdjson::error_code::SUCCESS ||
                coords.value().count_elements() != 2)
            {
                err |= coords.error();
                continue;
            }
            U32 arr_idx = 0;
            F64 coord_arr[2] = {};
            for (auto coord : coords)
            {
                F64 lat, lon;
                err |= coord.get_double().get(coord_arr[arr_idx]);

                arr_idx += 1;
            }

            if (utm_bbox.min.x < coord_arr[0] && coord_arr[0] < utm_bbox.max.x &&
                utm_bbox.min.y < coord_arr[1] && coord_arr[1] < utm_bbox.max.y)
            {
                *coords_buf[coord_idx] = {coord_arr[0], coord_arr[1]};
                coord_idx += 1;
            }
        }
        coords_buf.size = coord_idx;
        way_local->coords = coords_buf;

        if (err)
            break;
    }

    Buffer<neta_Edge> way_buffer = buffer_from_chunk_list<neta_Edge>(arena, way_local_list);
    Result<Buffer<neta_Edge>> res = {.v = way_buffer, .err = err};
    return res;
}

static Map<S64, neta_EdgeList>*
neta_osm_way_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox)
{
    prof_scope_marker;
    using namespace simdjson;

    Buffer<U8> buffer = io_file_read(arena, file_path);

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    simdjson::padded_string json_padded((char*)buffer.data, buffer.size);
    simdjson::error_code simd_error = parser.iterate(json_padded).get(doc);

    B32 err = false;
    Buffer<neta_Edge> edge_buf = {};
    if (simd_error == simdjson::error_code::SUCCESS)
    {
        Result<Buffer<neta_Edge>> res_edges = neta_edge_in_osm_area(arena, doc, utm_bbox);
        if (!res_edges.err)
            edge_buf = res_edges.v;
    }

    Map<S64, neta_EdgeList>* edge_map = map_create<S64, neta_EdgeList>(arena, 10);
    for (U32 i = 0; i < edge_buf.size; i++)
    {
        neta_Edge* edge = edge_buf[i];
        neta_EdgeList* list_res = map_get(edge_map, edge->osm_id);
        neta_EdgeNode* edge_node = PushStruct(arena, neta_EdgeNode);
        edge_node->edge = edge;

        if (!list_res)
        {
            neta_EdgeList dummy_edge_list = {};
            list_res = map_insert(edge_map, edge->osm_id, dummy_edge_list);
        }

        SLLQueuePush(list_res->first, list_res->last, edge_node);
    }

    return edge_map;
}
