namespace neta
{

g_internal void
neta_init()
{
    Arena* arena = arena_alloc();
    g_neta_state = PushStruct(arena, NetaState);
    AssertAlways(g_neta_state);

    B32 error = env_vars_value_get(arena, S("NETASCORE_API_KEY"), &g_neta_state->mobility_api_key, 1);
    if (error)
    {
        ERROR_LOG("NETASCORE_API_KEY environment variable could not be found");
    }
}

static Result<Buffer<Edge>>
_edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox)
{
    prof_scope_marker;
    using namespace simdjson;

    B32 err = false;
    ChunkList<Edge>* way_local_list = chunk_list_create<Edge>(arena, 7);

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
        Edge* way_local = chunk_list_get_next<Edge>(arena, way_local_list);
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
            if (coords.error() != simdjson::error_code::SUCCESS || coords.value().count_elements() != 2)
            {
                err |= coords.error();
                continue;
            }
            U32 arr_idx = 0;
            F64 coord_arr[2] = {};
            for (auto coord : coords)
            {
                err |= coord.get_double().get(coord_arr[arr_idx]);

                arr_idx += 1;
            }

            if (utm_bbox.min.x < coord_arr[0] && coord_arr[0] < utm_bbox.max.x && utm_bbox.min.y < coord_arr[1] && coord_arr[1] < utm_bbox.max.y)
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

    Buffer<Edge> way_buffer = buffer_from_chunk_list<Edge>(arena, way_local_list);
    Result<Buffer<Edge>> res = {.v = way_buffer, .err = err};
    return res;
}

static Map<S64, EdgeList>*
_osm_way_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox)
{
    prof_scope_marker;
    using namespace simdjson;

    Buffer<U8> buffer = io::file_read(arena, file_path);

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    simdjson::padded_string json_padded((char*)buffer.data, buffer.size);
    simdjson::error_code simd_error = parser.iterate(json_padded).get(doc);

    Buffer<Edge> edge_buf = {};
    if (simd_error == simdjson::error_code::SUCCESS)
    {
        Result<Buffer<Edge>> res_edges = _edge_in_osm_area(arena, doc, utm_bbox);
        if (!res_edges.err)
            edge_buf = res_edges.v;
    }

    Map<S64, EdgeList>* edge_map = map_create<S64, EdgeList>(arena, 10);
    for (U32 i = 0; i < edge_buf.size; i++)
    {
        Edge* edge = edge_buf[i];
        EdgeList* list_res = map_get(edge_map, edge->osm_id);
        EdgeNode* edge_node = PushStruct(arena, EdgeNode);
        edge_node->edge = edge;

        if (!list_res)
        {
            EdgeList dummy_edge_list = {};
            list_res = map_insert(edge_map, edge->osm_id, dummy_edge_list);
        }

        SLLQueuePush(list_res->first, list_res->last, edge_node);
    }

    return edge_map;
}

static String8
mobilitylab_jobs_api_get()
{
    return S("https://api.mobilitylab.zgis.at/citwin/jobs/");
}

static String8
mobilitylab_api_key_header_get(Arena* arena, String8 api_key)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    String8List parts = {};
    str8_list_push(scratch.arena, &parts, str8_lit("X-API-Key: "));
    str8_list_push(scratch.arena, &parts, api_key);

    String8 result = str8_list_join(arena, &parts, 0);
    return result;
}
static void
_netascore_download_enqueue(Arena* arena, NetaTaskState* task_state, String8 key, String8 file_name)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    NetascoreDownloadNode* node = PushStruct(arena, NetascoreDownloadNode);
    node->key = push_str8_copy(arena, key);
    node->file_name = push_str8_copy(arena, file_name);
    SLLQueuePush(download_queue->first, download_queue->last, node);
}

static async::HttpInfo<NetaTaskState>*
_netascore_next_download_http_info(Arena* arena, NetaTaskState* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    NetascoreDownloadNode* node = download_queue->first;
    if (node == 0)
    {
        return 0;
    }

    String8 download_api = push_str8f(arena, "%.*s%.*s/download/%.*s", str8_varg(mobilitylab_jobs_api_get()), str8_varg(download_queue->job_id), str8_varg(node->key));
    async::HttpInfo<NetaTaskState>* http_info = async::http_info_create_get<NetaTaskState>(arena, download_api, async::ContentType::None, {task_state->mobility_api_key_header});
    http_info->next_func = _netascore_download_file_complete;
    return http_info;
}

static async::UserFuncResult<NetaTaskState>
_netascore_download_file_complete(Arena* arena, String8 body, NetaTaskState* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    if (download_queue->first == 0)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Missing current NetAScore download file name");
    }
    String8 file_name = download_queue->first->file_name;
    SLLQueuePop(download_queue->first, download_queue->last);

    String8 file_path = str8_path_from_str8_list(arena, {dt_ctx_get()->data_dir, file_name});
    if (!os_write_data_to_file_path(file_path, body))
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Failed to write NetAScore download to %.*s", str8_varg(file_path));
    }

    async::HttpInfo<NetaTaskState>* next_http_info = _netascore_next_download_http_info(arena, task_state);
    if (next_http_info != 0)
    {
        return async::UserFuncResult<NetaTaskState>::success(next_http_info);
    }

    g_neta_state->edge_map = _osm_way_to_edges_map_create(g_neta_state->arena, file_path, task_state->utm_coords);
    if (!g_neta_state->edge_map)
    {
        exit_with_error("Failed to initialize neta");
    }
    return async::UserFuncResult<NetaTaskState>::success();
}

static async::UserFuncResult<NetaTaskState>
_netascore_downloads_complete(Arena* arena, String8 body, NetaTaskState* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Failed to parse NetAScore downloads response");
    }

    simdjson::dom::array downloads = {};
    err = doc.get_array().get(downloads);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Expected NetAScore downloads response to be an array");
    }

    for (simdjson::dom::element elem : downloads)
    {
        simdjson::dom::object item = {};
        err = elem.get_object().get(item);
        if (err)
        {
            return async::UserFuncResult<NetaTaskState>::failure(arena, "Failed to parse NetAScore download item");
        }

        const char* key = 0;
        const char* file_name = 0;
        simdjson::error_code err_key = item["key"].get_c_str().get(key);
        simdjson::error_code err_file_name = item["filename"].get_c_str().get(file_name);
        if (err_key || err_file_name)
        {
            return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore download item is missing key or filename");
        }

        String8 file_name_str = str8_c_string(file_name);
        if (str8_ends_with_lit(file_name_str, "_edges.geojson", MatchFlag_CaseInsensitive))
        {
            _netascore_download_enqueue(arena, task_state, str8_c_string(key), file_name_str);
            break;
        }
    }

    if (download_queue->first)
    {
        async::HttpInfo<NetaTaskState>* next_http_info = _netascore_next_download_http_info(arena, task_state);
        AssertAlways(next_http_info != 0);
        return async::UserFuncResult<NetaTaskState>::success(next_http_info);
    }
    return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore downloads response did not contain any *_edges.geojson files");
}

static async::UserFuncResult<NetaTaskState>
_netascore_job_status_complete(Arena* arena, String8 body, NetaTaskState* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Failed to parse NetAScore status response");
    }

    simdjson::dom::object obj = {};
    err = doc.get_object().get(obj);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Expected NetAScore status response to be an object");
    }

    const char* status = 0;
    const char* job_id = 0;
    simdjson::error_code err_status = obj["status"].get_c_str().get(status);
    simdjson::error_code err_job_id = obj["job_id"].get_c_str().get(job_id);
    if (err_status || err_job_id)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore status response is missing status or job_id");
    }

    download_queue->job_id = push_str8_copy(arena, str8_c_string(job_id));
    if (c_str_equal(status, "queued") || c_str_equal(status, "running"))
    {
        return async::UserFuncResult<NetaTaskState>::reschedule(1'000'000); // 1'000'000 ms = 1 sec delay
    }

    if (c_str_equal(status, "done"))
    {
        String8 downloads_api = push_str8f(arena, "%.*s%.*s/downloads", str8_varg(mobilitylab_jobs_api_get()), str8_varg(download_queue->job_id));
        async::HttpInfo<NetaTaskState>* http_info = async::http_info_create_get<NetaTaskState>(arena, downloads_api, async::ContentType::None, {task_state->mobility_api_key_header});
        http_info->next_func = _netascore_downloads_complete;
        return async::UserFuncResult<NetaTaskState>::success(http_info);
    }

    if (c_str_equal(status, "failed"))
    {
        const char* error_msg = 0;
        if (obj["error"].get_c_str().get(error_msg) == simdjson::SUCCESS && error_msg != 0)
        {
            return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore job %s failed: %s", job_id, error_msg);
        }
        return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore job %s failed", job_id);
    }

    return async::UserFuncResult<NetaTaskState>::failure(arena, "Unexpected NetAScore job status: %s", status);
}

static async::UserFuncResult<NetaTaskState>
netascore_job_create_complete(Arena* arena, String8 body, NetaTaskState* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Failed to parse NetAScore job creation response");
    }

    simdjson::dom::object obj = {};
    err = doc.get_object().get(obj);
    if (err)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Expected NetAScore job creation response to be an object");
    }

    const char* status = 0;
    const char* job_id = 0;
    simdjson::error_code err_status = obj["status"].get_c_str().get(status);
    simdjson::error_code err_job_id = obj["job_id"].get_c_str().get(job_id);
    if (err_status || err_job_id)
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "NetAScore job creation response is missing status or job_id");
    }

    if (!c_str_equal(status, "queued") && !c_str_equal(status, "running"))
    {
        return async::UserFuncResult<NetaTaskState>::failure(arena, "Unexpected NetAScore job creation status: %s", status);
    }

    *download_queue = {};
    download_queue->job_id = push_str8_copy(arena, str8_c_string(job_id));

    String8 status_api = push_str8f(arena, "%.*s%.*s", str8_varg(mobilitylab_jobs_api_get()), str8_varg(download_queue->job_id));
    async::HttpInfo<NetaTaskState>* http_info = async::http_info_create_get<NetaTaskState>(arena, status_api, async::ContentType::None, {task_state->mobility_api_key_header});
    http_info->next_func = _netascore_job_status_complete;
    return async::UserFuncResult<NetaTaskState>::success(http_info);
}

g_internal std::shared_ptr<async::AsyncCallCtx<neta::NetaTaskState>>
neta_task_state_create(String8 file, Rng2F64 utm_coords)
{
    std::shared_ptr<async::AsyncCallCtx<neta::NetaTaskState>> netascore_task_ctx = async::async_task_create<neta::NetaTaskState>();
    netascore_task_ctx->task_state.mobility_api_key_header = neta::mobilitylab_api_key_header_get(netascore_task_ctx->arena, neta::g_neta_state->mobility_api_key);
    netascore_task_ctx->task_state.utm_coords = utm_coords;
    netascore_task_ctx->task_state.file_path = push_str8_copy(netascore_task_ctx->arena, file);

    return netascore_task_ctx;
}

g_internal async::AsyncResult
netascore_async_task_create(String8 cache_path, Vec2F64 btm_right_corner_wgs84)
{
    Context* ctx = dt_ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    Vec2F64 bbox_size_meters = util::default_bbox_size_meters_get();
    Rng2F64 bbox = util::wgs84_bbox_from_btm_right_corner(btm_right_corner_wgs84, bbox_size_meters);
    S32 target_srid = util::target_srid_from_wgs84(btm_right_corner_wgs84);
    if (target_srid == 0)
    {
        exit_with_error("Failed to derive a valid UTM SRID from the provided WGS84 coordinates");
    }

    String8List bbox_param_list = {};
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.min.y));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.min.x));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.max.y));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.max.x));
    StringJoin sep = {.pre = S("bbox_str="), .sep = S(","), .post = S("")};
    String8 bbox_str = str8_list_join(scratch.arena, &bbox_param_list, &sep);
    Rng2F64 utm_coords = util::utm_from_wgs84(bbox);
    String8 neta_file_path = str8_path_from_str8_list(scratch.arena, {cache_path, S("netascore_edges.geojson")});

    Result<String8> netascore_result = cache_read(g_neta_state->arena, neta_file_path, bbox_str);
    async::AsyncResult async_result = {};
    if (netascore_result.err)
    {
        std::shared_ptr<async::AsyncCallCtx<neta::NetaTaskState>> task_state = neta::neta_task_state_create(neta_file_path, utm_coords);
        String8 netascore_api = push_str8f(scratch.arena, "%.*snetascore", str8_varg(neta::mobilitylab_jobs_api_get()));
        async_result = async::async_http_task_create(task_state, ctx->thread_pool, HTTP_Method_Post, netascore_api, async::ContentType::FormUrlEncoded,
                                                     {push_str8f(scratch.arena, "target_srid=%d", target_srid), bbox_str, S("output_format=GeoJSON")}, {task_state->task_state.mobility_api_key_header},
                                                     neta::netascore_job_create_complete, 5, 1);
    }
    return async_result;
}
} // namespace neta
