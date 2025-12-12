#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"

struct neta_Edge
{
    neta_Edge* next;
    S64 edge_id;
    S64 osm_id;
    F64 index_bike_ft;
    F64 index_bike_tf;
    F64 index_walk_ft;
    F64 index_walk_tf;
    Buffer<Vec2F64> coords;
};

struct neta_EdgeNode
{
    neta_EdgeNode* next;
    neta_Edge* edge;
};

struct neta_EdgeList
{
    neta_EdgeNode* first;
    neta_EdgeNode* last;
};

static Map<S64, neta_EdgeList>*
neta_osm_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox);

// private fields
static Result<Buffer<neta_Edge>>
neta_edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox);
//////////////////////////////////////////////////////////////////////////////////////
