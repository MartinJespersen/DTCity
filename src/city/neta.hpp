#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"

namespace neta
{

struct Edge
{
    Edge* next;
    S64 edge_id;
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

    String8 mobility_api_key;
};

struct dt_NetascoreDownloadNode
{
    dt_NetascoreDownloadNode* next;
    String8 key;
    String8 file_name;
};

struct dt_NetascoreDownloadQueue
{
    dt_NetascoreDownloadNode* first;
    dt_NetascoreDownloadNode* last;
    String8 current_file_name;
    String8 job_id;
    B32 wrote_primary_geojson;
};

struct NetaTaskState
{
    String8 mobility_api_key_header;
};
// Global State ////////////////////////////
g_internal dt_NetascoreDownloadQueue g_netascore_download_queue;
g_internal NetaState* g_neta_state = 0;
/////////////////////////////////////////////
g_internal void
neta_init();

g_internal Map<S64, EdgeList>*
osm_way_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox);

// private fields
g_internal Result<Buffer<Edge>>
edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox);

g_internal String8
mobilitylab_jobs_api_get();

g_internal String8
mobilitylab_api_key_header_get(Arena* arena, String8 api_key);
g_internal void
_netascore_download_enqueue(Arena* arena, String8 key, String8 file_name);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_download_file_complete(Arena* arena, String8 body, NetaTaskState* task_state);

g_internal async::HttpInfo<NetaTaskState>*
_netascore_next_download_http_info(Arena* arena, String8 mobility_api_key_header);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_downloads_complete(Arena* arena, String8 body, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
_netascore_job_status_complete(Arena* arena, String8 body, NetaTaskState* task_state);

g_internal async::UserFuncResult<NetaTaskState>
netascore_job_create_complete(Arena* arena, String8 body, NetaTaskState* task_state);
} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
