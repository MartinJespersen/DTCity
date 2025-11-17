#pragma once

struct osm_RoadNode
{
    osm_RoadNode* next;
    U64 id;
    F32 lat;
    F32 lon;
};

struct osm_Tag
{
    osm_Tag* next;
    String8 key;
    String8 value;
};

enum osm_TagResultEnum
{
    ROAD_TAG_FOUND = 0,
    ROAD_TAG_NOT_FOUND = 1,
};

struct osm_TagResult
{
    osm_TagResultEnum result;
    String8 value;
};

struct osm_RoadNodeList
{
    osm_RoadNode* first;
    osm_RoadNode* last;
};

struct osm_RoadNodeParseResult
{
    Buffer<osm_RoadNodeList> road_nodes;
    B32 error;
};

union osm_GCSBoundingBox
{
    struct
    {
        Vec2F64 btm_left;
        Vec2F64 top_right;
    };
    struct
    {
        F64 lat_btm_left;
        F64 lon_btm_left;
        F64 lat_top_right;
        F64 lon_top_right;
    };
};

struct osm_Way
{
    osm_Way* next;

    U64 id;

    U64* node_ids; // fixed array with node_count lenght and node ids as index
    U64 node_count;

    Buffer<osm_Tag> tags;
};

struct osm_WayNode
{
    osm_WayNode* next;
    osm_WayNode* hash_next;
    osm_Way way;
};

struct osm_WayList
{
    osm_WayNode* first;
    osm_WayNode* last;
};

struct osm_WayParseResult
{
    Buffer<osm_Way> ways;
    B32 error;
};

struct osm_UtmNode
{
    osm_UtmNode* next;
    U64 id;
    union
    {
        Vec2F32 pos;
        glm::vec2 vec;
    };
    String8 utm_zone;
    osm_WayList way_queue; // Linked list of RoadWays sharing this node
};

struct osm_UtmNodeList
{
    osm_UtmNode* first;
    osm_UtmNode* last;
};

enum osm_KeyType
{
    OsmKeytype_Road,
    OsmKeyType_Building,
    OsmKeytype_Count
};

struct osm_Network
{
    Arena* arena;
    Buffer<osm_UtmNodeList> utm_node_hashmap; // key is the node id
    Vec2F64 utm_center_offset; // used for centering utm coordinate based on bounding box

    Buffer<osm_WayList> way_hashmap;            // view into way buffers
    Buffer<osm_Way> ways_arr[OsmKeytype_Count]; // buffer storage
};

// ~mgj: Globals
static osm_Network* osm_g_network = {};
read_only static osm_UtmNode osm_g_road_node_utm = {&osm_g_road_node_utm, 0, 0.0f, 0.0f};
///////////////////////

static void
osm_structure_init(U64 node_hashmap_size, U64 way_hashmap_size, osm_GCSBoundingBox* gcs_bbox);
static void
osm_structure_cleanup();
static void
osm_structure_add(osm_Network* node_utm_structure, Buffer<osm_RoadNodeList> node_hashmap,
                  String8 json, osm_KeyType osm_key_type);
static osm_TagResult
osm_tag_find(Arena* arena, Buffer<osm_Tag> tags, String8 tag_to_find);
static osm_WayNode*
osm_way_find(U64 way_id);
static osm_UtmNode*
osm_utm_node_find(U64 node_id);
static osm_UtmNode*
osm_random_utm_node_get();
static B32
osm_node_hashmap_insert(U64 node_id, osm_Way* way, osm_UtmNode** out);
static osm_RoadNode*
osm_node_find(Buffer<osm_RoadNodeList> node_hashmap, U64 node_id);

static osm_UtmNode*
osm_random_neighbour_node_get(osm_UtmNode* node);
