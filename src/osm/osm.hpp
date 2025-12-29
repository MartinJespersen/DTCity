#pragma once

namespace osm
{

typedef U64 NodeId;
typedef U64 WayId;

enum class Result : B32
{
    Success = 0,
    NotFound = (1 << 0),
};

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

struct UtmLocation
{
    NodeId id;
    union
    {
        Vec2F32 pos;
        glm::vec2 vec;
    };
};

UtmLocation
utm_location_create(U64 id, Vec2F32 pos)
{
    UtmLocation loc = {.id = id, .pos = pos};
    return loc;
}

struct NodeList
{
    Node* first;
    Node* last;
};

enum WayType
{
    WayType_Road,
    WayType_Building,
    WayType_Count
};

struct Network
{
    Arena* arena;
    Buffer<NodeList> utm_node_hashmap; // key is the node id
    Vec2F64 utm_center_offset;         // used for centering utm coordinate based on bounding box
    ChunkList<UtmLocation>* utm_location_chunk_list;

    Buffer<WayList> way_hashmap;         // view into way buffers
    Buffer<Way> ways_arr[WayType_Count]; // buffer storage
    Buffer<NodeId> node_id_arr[WayType_Count];
};

///////////////////////
// ~mgj: Globals
g_internal Network* g_network = {};
read_only g_internal Node g_road_node_utm = {nullptr, 0, {}, {}};
///////////////////////

// Public
g_internal void
structure_init(U64 node_hashmap_size, U64 way_hashmap_size, Rng2F64 utm_coords);
g_internal void
structure_cleanup();
g_internal void
structure_add(Buffer<RoadNodeList> node_hashmap, String8 json, WayType key_type);
g_internal TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find);
g_internal WayNode*
way_find(U64 way_id);
g_internal UtmLocation
utm_location_get(U64 node_id);
g_internal Node*
node_get(U64 node_id);
g_internal Node*
random_neighbour_node_get(Node* node);
g_internal Node*
random_neighbour_node_get(U64 node_id);
g_internal NodeId
random_node_id_from_type_get(WayType type);

// Privates
g_internal WgsNode*
wgs_node_find(Buffer<RoadNodeList> node_hashmap, U64 node_id);

g_internal B32
node_hashmap_insert(U64 node_id, Way* way, Node** out);
} // namespace osm
