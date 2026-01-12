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

static Map<S64, EdgeList>*
osm_way_to_edges_map_create(Arena* arena, String8 file_path, Rng2F64 utm_bbox);

// private fields
static Result<Buffer<Edge>>
edge_in_osm_area(Arena* arena, simdjson::ondemand::document& doc, Rng2F64 utm_bbox);

} // namespace neta
//////////////////////////////////////////////////////////////////////////////////////
