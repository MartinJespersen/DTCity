#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"

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

struct NetaState
{
    Arena* arena;

    std::atomic<B32> data_downloaded;
    String8 mobility_api_key;
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
    String8 file_path;
    String8 cache_hash_input;
};

// Global State ////////////////////////////
g_internal NetaState* g_neta_state = 0;
/////////////////////////////////////////////
g_internal void
neta_init();

g_internal async::UserFuncResult<NetaTaskState>
netascore_job_create_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal String8
mobilitylab_jobs_api_get();

g_internal String8
mobilitylab_api_key_header_get(Arena* arena, String8 api_key);

g_internal async::AsyncHttpTaskCreateResult<NetaTaskState>
netascore_async_task_create(String8 cache_path, Rng2F64 bbox);

// private fields
g_internal Result<Buffer<Edge>>
_edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 bbox_wgs84);

g_internal void
_netascore_download_enqueue(Arena* arena, NetaTaskState* task_state, String8 key, String8 file_name);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_download_file_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal async::HttpInfo<NetaTaskState>*
_netascore_next_download_http_info(Arena* arena, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_downloads_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_job_status_complete(Arena* arena, async::ThreadPool* thread_pool, String8 body, NetaTaskState* task_state);

} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
