#pragma once

#include "async/async_http.hpp"
#include "osm/osm.hpp"
#include "third_party/simdjson/simdjson.h"

namespace async
{
struct ThreadPool;
struct HttpInfo;
template <typename T>
struct UserFuncResult;
} // namespace async

namespace neta
{

struct Edge
{
    Edge* next;
    S64 osm_id;
    F64 index_bike_ft;
    F64 index_bike_tf;
    F64 index_walk_ft;
    F64 index_walk_tf;
    Buffer<Vec2F64> coords;
};

struct EdgeNode
{
    EdgeNode* next;
    Edge* edge;
};

struct EdgeList
{
    EdgeNode* first;
    EdgeNode* last;
};

struct NetascoreDownloadNode
{
    NetascoreDownloadNode* next;
    String8 key;
    String8 file_name;
};

struct NetascoreDownloadQueue
{
    NetascoreDownloadNode* first;
    NetascoreDownloadNode* last;
    String8 job_id;
};

struct NetaCache
{
    String8 file_location;
    String8 bbox_str;
};

// Completed cache result. Request credentials and queues belong to NetaLoadTask.
struct NetaState
{
    Allocator allocator;
    String8 cache_file_location;

    explicit NetaState(String8 file_location) noexcept;
};

struct NetaLoadTask
{
    NetaCache cache;
    Rng2F64 bbox_wgs84;
    String8 mobility_api_key_header;
    NetascoreDownloadQueue download_queue;
    std::unique_ptr<NetaState> result;
};

async::AsyncTaskStatus<NetaLoadTask>*
neta_load(async::ThreadPool* thread_pool, NetaCache cache, Rng2F64 bbox);

g_internal async::UserFuncResult<NetaLoadTask>
netascore_job_create_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state);

g_internal String8
mobilitylab_jobs_api_get();

g_internal String8
mobilitylab_api_key_header_get(Arena* arena, String8 api_key);

g_internal Map<S64, EdgeList>*
osm_way_to_edges_map_create(Arena* arena, osm::Network* network, String8 file_path, Rng2F64 bbox_wgs84);

// private fields
g_internal async::AsyncTaskContinuation<NetaLoadTask>
_neta_load_begin(async::ThreadInfo thread_info, async::AsyncTaskStatus<NetaLoadTask>* task);
g_internal Result<Buffer<Edge>>
_edge_in_osm_area(Arena* arena, osm::Network* network, simdjson::ondemand::document& doc, Rng2F64 bbox_wgs84);

g_internal void
_netascore_download_enqueue(Arena* arena, NetaLoadTask* task_state, String8 key, String8 file_name);

g_internal async::UserFuncResult<NetaLoadTask>
_netascore_download_file_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state);

g_internal async::HttpInfo*
_netascore_next_download_http_info(Arena* arena, NetaLoadTask* task_state);

g_internal async::UserFuncResult<NetaLoadTask>
_netascore_downloads_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state);

g_internal async::UserFuncResult<NetaLoadTask>
_netascore_job_status_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaLoadTask* task_state);

} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
