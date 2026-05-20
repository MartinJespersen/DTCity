#pragma once

namespace neta
{
struct EdgeList;
}
namespace city
{
struct City;
};

namespace osm
{

typedef S64 EdgeId;
typedef U64 NodeId;
typedef S64 WayId;

struct WgsNode
{
    WgsNode* next;
    NodeId id;
    F32 lat;
    F32 lon;
};

struct Tag
{
    Tag* next;
    String8 key;
    String8 value;
};

enum class TagResultEnum : int
{
    ROAD_TAG_FOUND = 0,
    ROAD_TAG_NOT_FOUND = 1,
};

struct TagResult
{
    TagResultEnum result;
    String8 value;
};

struct RoadNodeList
{
    WgsNode* first;
    WgsNode* last;
};

struct RoadNodeParseResult
{
    Buffer<RoadNodeList> road_nodes;
    B32 error;
};

struct Way
{
    Way* next;

    WayId id;

    NodeId* node_ids; // fixed array with node_count length and node ids as index
    U64 node_count;

    Buffer<Tag> tags;
};

struct WayNode
{
    WayNode* next;
    WayNode* hash_next;
    Way way;
};

struct WayList
{
    WayNode* first;
    WayNode* last;
};

struct WayParseResult
{
    Buffer<Way> ways;
    B32 error;
};

struct Node
{
    Node* next;
    NodeId id;

    String8 utm_zone;  // TODO: If not used in future: Delete
    WayList way_queue; // Linked list of RoadWays sharing this node
};

struct EcefLocation
{
    NodeId id;
    Vec3F64 pos;
};

struct WgsLocation
{
    NodeId id;
    F64 lat;
    F64 lon;
};

struct RoadEdge
{
    RoadEdge* prev;
    RoadEdge* next;
    S64 id;
    S64 node_id_from;
    S64 node_id_to;
    S64 way_id;
};

struct EdgeStructure
{
    Buffer<RoadEdge> edges;
    Map<EdgeId, RoadEdge*> edge_map;
};
EcefLocation
ecef_location_create(U64 id, Vec3F64 pos)
{
    EcefLocation loc = {.id = id, .pos = pos};
    return loc;
}

struct NodeList
{
    Node* first;
    Node* last;
};

#define WAYTYPE_OPTIONS                                                                                                                                                                                \
    X(Building, "building")                                                                                                                                                                            \
    X(Highway, "highway")

enum class WayType : U32
{
#define X(name, str) name,
    WAYTYPE_OPTIONS
#undef X
        Count
};

read_only g_internal const char* g_waytype_osm_tag[] = {
#define X(name, str) str,
    WAYTYPE_OPTIONS
#undef X
};

struct Network
{
    Arena* arena;

    Buffer<NodeList> node_hashmap; // key is the node id
    Map<NodeId, EcefLocation>* ecef_location_map;
    Map<NodeId, WgsLocation>* wgs_location_map;

    Buffer<WayList> way_hashmap;                    // view into way buffers
    Buffer<Way> ways_arr[enum_idx(WayType::Count)]; // buffer storage
    Buffer<NodeId> node_id_arr[enum_idx(WayType::Count)];
    EdgeStructure edge_structure;
    Map<S64, neta::EdgeList>* neta_edge_map;
};

///////////////////////
// ~mgj: Globals
g_internal Network* g_network = {};
read_only g_internal Node g_road_node_utm = {nullptr, 0, {}, {}};
///////////////////////

// Public
g_internal void
structure_init(U64 node_hashmap_size, U64 way_hashmap_size);
g_internal void
structure_cleanup();
g_internal TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find);
g_internal WayNode*
way_find(WayId way_id);
g_internal EcefLocation
location_get(U64 node_id);
g_internal WgsLocation
wgs_location_get(U64 node_id);
g_internal Node*
node_get(U64 node_id);
g_internal Node*
random_neighbour_node_get(Node* node);
g_internal Node*
random_neighbour_node_get(U64 node_id);
g_internal NodeId
random_node_id_from_type_get(WayType type);

g_internal async::UserFuncResult<city::City>
parse_osm_data(Arena* arena, async::ThreadPool* thread_pool, String8 response_body, city::City* task_state);
// Privates
g_internal void
_road_edge_structure_create();
g_internal WgsNode*
_wgs_node_find(Buffer<RoadNodeList> node_hashmap, U64 node_id);
g_internal B32
_node_hashmap_insert(U64 node_id, Way* way, Node** out);
} // namespace osm
