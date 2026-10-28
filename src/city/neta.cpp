#include "core_inc.hpp"
#include "base/cache.hpp"
#include "async/async_inc.hpp"
#include "utility/geospatial.hpp"
#include "city/neta.hpp"

namespace neta
{

NetaState::NetaState(String8 file_location) noexcept
    : allocator(Allocator::create())
{
    Debug_SetName(this->allocator.arena, "Neta cache result");
    this->cache_file_location = push_str8_copy(this->allocator.arena, file_location);
}

async::AsyncTaskStatus<NetaLoadTask>*
neta_load(async::ThreadPool* thread_pool, NetaCache cache, Rng2F64 bbox)
{
    Allocator allocator = Allocator::create();
    Debug_SetName(allocator.arena, "Neta load task");
    auto* input = allocator.make<NetaLoadTask>();
    input->cache.file_location = push_str8_copy(allocator.arena, cache.file_location);
    input->cache.bbox_str = push_str8_copy(allocator.arena, cache.bbox_str);
    input->bbox_wgs84 = bbox;
    return async::async_task_run(std::move(allocator), thread_pool, _neta_load_begin, input, "Neta load task");
}

static Result<Buffer<Edge>>
_edge_in_osm_area(Arena* arena, osm::Network* network, simdjson::ondemand::document& doc, Rng2F64 bbox_wgs84)
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

        osm::WayNode* osm_way = osm::way_find(network, osm_id);
        if (!osm_way) // do not save road ways not in the osm database
            continue;

        // insert into buffer to hold neta information for each edge/way
        Edge* way_local = chunk_list_get_next<Edge>(arena, way_local_list);
        way_local->osm_id = osm_id;
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
        size_t size = 0;
        err |= geometry.count_elements().get(size);
        if (err)
        {
            break;
        }
        Buffer<Vec2F64> coords_buf = buffer_alloc<Vec2F64>(arena, size);
        U64 coord_idx = 0;
        for (auto point : geometry)
        {
            auto coords = point.get_array();
            size_t coord_count = 0;
            simdjson::error_code coord_error = coords.count_elements().get(coord_count);
            if (coord_error != simdjson::error_code::SUCCESS || coord_count != 2)
            {
                err |=
                    coord_error != simdjson::error_code::SUCCESS ? coord_error : simdjson::error_code::INCORRECT_TYPE;
                continue;
            }
            U32 arr_idx = 0;
            F64 coord_arr[2] = {};
            for (auto coord : coords)
            {
                err |= coord.get_double().get(coord_arr[arr_idx]);

                arr_idx += 1;
            }

            if (bbox_wgs84.min.x < coord_arr[0] && coord_arr[0] < bbox_wgs84.max.x && bbox_wgs84.min.y < coord_arr[1] &&
                coord_arr[1] < bbox_wgs84.max.y)
            {
                coords_buf[coord_idx] = {coord_arr[0], coord_arr[1]};
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
osm_way_to_edges_map_create(Arena* arena, osm::Network* network, String8 file_path, Rng2F64 bbox_wgs84)
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
        Result<Buffer<Edge>> res_edges = _edge_in_osm_area(arena, network, doc, bbox_wgs84);
        if (!res_edges.err)
        {
            edge_buf = res_edges.v;
        }
        else
        {
            ERROR_LOG("Error in netascore json deserialization");
        }
    }

    Map<S64, EdgeList>* edge_map = Map<S64, EdgeList>::create(arena, 10);
    for (U32 i = 0; i < edge_buf.size; i++)
    {
        Edge* edge = &edge_buf[i];
        EdgeList* list_res = edge_map->get(edge->osm_id);
        EdgeNode* edge_node = PushStruct(arena, EdgeNode);
        edge_node->edge = edge;

        if (!list_res)
        {
            EdgeList dummy_edge_list = {};
            list_res = edge_map->insert(arena, edge->osm_id, dummy_edge_list);
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
_netascore_download_enqueue(Arena* arena, NetaLoadTask* task_state, String8 key, String8 file_name)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    NetascoreDownloadNode* node = PushStruct(arena, NetascoreDownloadNode);
    node->key = push_str8_copy(arena, key);
    node->file_name = push_str8_copy(arena, file_name);
    SLLQueuePush(download_queue->first, download_queue->last, node);
}

static async::HttpInfo*
_netascore_next_download_http_info(Arena* arena, NetaLoadTask* task_state)
{
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    NetascoreDownloadNode* node = download_queue->first;
    if (node == 0)
    {
        return 0;
    }

    String8 download_api = push_str8f(arena, "%.*s%.*s/download/%.*s", str8_varg(mobilitylab_jobs_api_get()),
                                      str8_varg(download_queue->job_id), str8_varg(node->key));
    async::HttpInfo* http_info =
        async::http_info_create_get(arena, download_api, {task_state->mobility_api_key_header});
    return http_info;
}

static async::UserFuncResult<NetaLoadTask>
_netascore_download_file_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state)
{
    (void)thread_pool;
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    if (download_queue->first == 0)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Missing current NetAScore download file name");
    }
    Error err = cache_write(task_state->cache.file_location, body, task_state->cache.bbox_str);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Could not cache netascore value");
    }
    // Keep the current download queued until caching succeeds, so retries can repeat it.
    SLLQueuePop(download_queue->first, download_queue->last);
    task_state->result = std::make_unique<NetaState>(task_state->cache.file_location);

    return async::UserFuncResult<NetaLoadTask>::success();
}

static async::UserFuncResult<NetaLoadTask>
_netascore_downloads_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state)
{
    (void)thread_pool;
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Failed to parse NetAScore downloads response");
    }

    simdjson::dom::array downloads = {};
    err = doc.get_array().get(downloads);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena,
                                                             "Expected NetAScore downloads response to be an array");
    }

    for (simdjson::dom::element elem : downloads)
    {
        simdjson::dom::object item = {};
        err = elem.get_object().get(item);
        if (err)
        {
            return async::UserFuncResult<NetaLoadTask>::failure(arena, "Failed to parse NetAScore download item");
        }

        const char* key = 0;
        const char* file_name = 0;
        simdjson::error_code err_key = item["key"].get_c_str().get(key);
        simdjson::error_code err_file_name = item["filename"].get_c_str().get(file_name);
        if (err_key || err_file_name)
        {
            return async::UserFuncResult<NetaLoadTask>::failure(arena,
                                                                 "NetAScore download item is missing key or filename");
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
        async::HttpInfo* next_http_info = _netascore_next_download_http_info(arena, task_state);
        AssertAlways(next_http_info != 0);
        return async::UserFuncResult<NetaLoadTask>::success(next_http_info, _netascore_download_file_complete);
    }
    return async::UserFuncResult<NetaLoadTask>::failure(
        arena, "NetAScore downloads response did not contain any *_edges.geojson files");
}

static async::UserFuncResult<NetaLoadTask>
_netascore_job_status_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state)
{
    (void)thread_pool;
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Failed to parse NetAScore status response");
    }

    simdjson::dom::object obj = {};
    err = doc.get_object().get(obj);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena,
                                                             "Expected NetAScore status response to be an object");
    }

    const char* status = 0;
    const char* job_id = 0;
    simdjson::error_code err_status = obj["status"].get_c_str().get(status);
    simdjson::error_code err_job_id = obj["job_id"].get_c_str().get(job_id);
    if (err_status || err_job_id)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena,
                                                             "NetAScore status response is missing status or job_id");
    }

    download_queue->job_id = push_str8_copy(arena, str8_c_string(job_id));
    if (c_str_equal(status, "queued") || c_str_equal(status, "running"))
    {
        return async::UserFuncResult<NetaLoadTask>::reschedule(1'000'000); // 1'000'000 ms = 1 sec delay
    }

    if (c_str_equal(status, "done"))
    {
        String8 downloads_api = push_str8f(arena, "%.*s%.*s/downloads", str8_varg(mobilitylab_jobs_api_get()),
                                           str8_varg(download_queue->job_id));
        async::HttpInfo* http_info =
            async::http_info_create_get(arena, downloads_api, {task_state->mobility_api_key_header});
        return async::UserFuncResult<NetaLoadTask>::success(http_info, _netascore_downloads_complete);
    }

    if (c_str_equal(status, "failed"))
    {
        const char* error_msg = 0;
        if (obj["error"].get_c_str().get(error_msg) == simdjson::SUCCESS && error_msg != 0)
        {
            return async::UserFuncResult<NetaLoadTask>::failure(arena, "NetAScore job %s failed: %s", job_id,
                                                                 error_msg);
        }
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "NetAScore job %s failed", job_id);
    }

    return async::UserFuncResult<NetaLoadTask>::failure(arena, "Unexpected NetAScore job status: %s", status);
}

static async::UserFuncResult<NetaLoadTask>
netascore_job_create_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state)
{
    (void)thread_pool;
    NetascoreDownloadQueue* download_queue = &task_state->download_queue;
    simdjson::dom::parser parser;
    simdjson::dom::element doc = {};
    simdjson::error_code err = parser.parse(body.str, body.size).get(doc);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Failed to parse NetAScore job creation response");
    }

    simdjson::dom::object obj = {};
    err = doc.get_object().get(obj);
    if (err)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(
            arena, "Expected NetAScore job creation response to be an object");
    }

    const char* status = 0;
    const char* job_id = 0;
    simdjson::error_code err_status = obj["status"].get_c_str().get(status);
    simdjson::error_code err_job_id = obj["job_id"].get_c_str().get(job_id);
    if (err_status || err_job_id)
    {
        return async::UserFuncResult<NetaLoadTask>::failure(
            arena, "NetAScore job creation response is missing status or job_id");
    }

    if (!c_str_equal(status, "queued") && !c_str_equal(status, "running"))
    {
        return async::UserFuncResult<NetaLoadTask>::failure(arena, "Unexpected NetAScore job creation status: %s",
                                                             status);
    }

    *download_queue = {};
    download_queue->job_id = push_str8_copy(arena, str8_c_string(job_id));

    String8 status_api =
        push_str8f(arena, "%.*s%.*s", str8_varg(mobilitylab_jobs_api_get()), str8_varg(download_queue->job_id));
    async::HttpInfo* http_info = async::http_info_create_get(arena, status_api, {task_state->mobility_api_key_header});
    return async::UserFuncResult<NetaLoadTask>::success(http_info, _netascore_job_status_complete);
}

g_internal async::AsyncTaskContinuation<NetaLoadTask>
_neta_load_begin(async::ThreadInfo thread_info, async::AsyncTaskStatus<NetaLoadTask>* task)
{
    (void)thread_info;
    NetaLoadTask* input = task->user_data;
    Arena* arena = task->allocator.arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Result<String8> cached = cache_read(scratch.arena, input->cache.file_location, input->cache.bbox_str);
    if (!cached.err)
    {
        input->result = std::make_unique<NetaState>(input->cache.file_location);
        return {};
    }

    // Credentials are needed only for a download and never enter the published result.
    String8 api_key = {};
    if (env_vars_value_get(scratch.arena, S("NETASCORE_API_KEY"), &api_key, 1) || api_key.size == 0)
    {
        ERROR_LOG("NETASCORE_API_KEY environment variable could not be found");
        task->add_error();
        return {};
    }
    S32 target_srid = util::target_srid_from_wgs84(input->bbox_wgs84.min);
    if (target_srid == 0)
    {
        ERROR_LOG("Failed to derive a UTM SRID for NetAScore");
        task->add_error();
        return {};
    }
    input->mobility_api_key_header = mobilitylab_api_key_header_get(arena, api_key);
    String8 api = push_str8f(arena, "%.*snetascore", str8_varg(mobilitylab_jobs_api_get()));
    String8 srid = push_str8f(arena, "target_srid=%d", target_srid);
    async::HttpInfo* info = async::http_info_create(arena, HTTP_Method_Post, api,
        S("application/x-www-form-urlencoded"), {input->mobility_api_key_header},
        {srid, input->cache.bbox_str, S("output_format=GeoJSON")});

    auto* http = task->allocator.make<async::AsyncHttpTaskState<NetaLoadTask>>();
    http->curl_ctx = async::_curl_ctx_create(arena);
    http->thread_pool = task->thread_pool;
    http->first_func = http->next_func = netascore_job_create_complete;
    http->first_http_info = http->next_http_info = info;
    http->max_http_retries = 5;
    http->max_task_retries = 1;
    http->timeout_us = 300'000'000;
    http->task_start_us = os_now_microseconds();
    task->http_ext = http;
    task->ext_type = async::ExtensionType::Http;
    if (task->http_reconfigure(info)) return {};
    return {.func = async::_http_main<NetaLoadTask>};
}

} // namespace neta
