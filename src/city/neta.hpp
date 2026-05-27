#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"
namespace city
{
struct AsyncCityTask;
};

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

struct NetaTaskState
{
    String8 mobility_api_key_header;
    NetascoreDownloadQueue download_queue;
    Rng2F64 bbox_wgs84;
    String8 cache_file_location;
    String8 cache_bbox_str;
    std::atomic<B32> data_downloaded;
};

struct NetaState
{
    Arena* arena;

    String8 mobility_api_key;
    String8 cache_file_location;

    NetaTaskState task_state;
};

g_internal void
neta_init(NetaState* neta_state, String8 cache_path, String8 cache_type, Rng2F64 bbox, String8 bbox_cache_str);

g_internal async::UserFuncResult<NetaTaskState>
netascore_job_create_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal String8
mobilitylab_jobs_api_get();

g_internal String8
mobilitylab_api_key_header_get(Arena* arena, String8 api_key);

g_internal city::AsyncCityTask*
netascore_async_task_create(Arena* arena, NetaState* neta, Rng2F64 bbox);

g_internal Map<S64, EdgeList>*
osm_way_to_edges_map_create(Arena* arena, osm::Network* network, String8 file_path, Rng2F64 bbox_wgs84);

// private fields
g_internal Result<Buffer<Edge>>
_edge_in_osm_area(Arena* arena, osm::Network* network, simdjson::ondemand::document& doc, Rng2F64 bbox_wgs84);

g_internal void
_netascore_download_enqueue(Arena* arena, NetaTaskState* task_state, String8 key, String8 file_name);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_download_file_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal async::HttpInfo*
_netascore_next_download_http_info(Arena* arena, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_downloads_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_job_status_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
