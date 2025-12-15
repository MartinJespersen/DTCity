#pragma once

namespace osm
{

enum class Result : B32
{
    Success = 0,
    NotFound = (1 << 0),
};

struct RoadNode
{
    RoadNode* next;
    U64 id;
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
    RoadNode* first;
    RoadNode* last;
};

struct RoadNodeParseResult
{
    Buffer<RoadNodeList> road_nodes;
    B32 error;
};

struct Way
{
    Way* next;

    U64 id;

    U64* node_ids; // fixed array with node_count length and node ids as index
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

struct UtmNode
{
    UtmNode* next;
    U64 id;
    union
    {
        Vec2F32 pos;
        glm::vec2 vec;
    };
    String8 utm_zone;
    WayList way_queue; // Linked list of RoadWays sharing this node
};

struct UtmNodeList
{
    UtmNode* first;
    UtmNode* last;
};

enum OsmKeyType
{
    OsmKeyType_Road,
    OsmKeyType_Building,
    OsmKeyType_Count
};

struct Network
{
    Arena* arena;
    Buffer<UtmNodeList> utm_node_hashmap; // key is the node id
    Vec2F64 utm_center_offset;            // used for centering utm coordinate based on bounding box

    Buffer<WayList> way_hashmap;            // view into way buffers
    Buffer<Way> ways_arr[OsmKeyType_Count]; // buffer storage
};

///////////////////////
// ~mgj: Globals
static Network* g_network = {};
read_only static UtmNode g_road_node_utm = {nullptr, 0, {0.0f, 0.0f}, {}, {}};
///////////////////////

// Function declarations
static void
structure_init(U64 node_hashmap_size, U64 way_hashmap_size, Rng2F64 utm_coords);
static void
structure_cleanup();
static void
structure_add(Buffer<RoadNodeList> node_hashmap, String8 json, OsmKeyType key_type);
static TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find);
static WayNode*
way_find(U64 way_id);
static UtmNode*
utm_node_find(U64 node_id);
static UtmNode*
random_utm_node_get();
static B32
node_hashmap_insert(U64 node_id, Way* way, UtmNode** out);
static RoadNode*
node_find(Buffer<RoadNodeList> node_hashmap, U64 node_id);

static UtmNode*
random_neighbour_node_get(UtmNode* node);

} // namespace osm
