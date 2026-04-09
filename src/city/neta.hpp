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

// Global State ////////////////////////////
g_internal NetaState* g_neta_state = 0;
/////////////////////////////////////////////
g_internal void
neta_init();

static Map<S64, EdgeList>*
osm_way_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox);

// private fields
static Result<Buffer<Edge>>
edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox);

} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
