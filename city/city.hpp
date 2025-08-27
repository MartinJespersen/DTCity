#pragma once

namespace city
{

struct RoadNode
{
    RoadNode* next;
    U64 id;
    F32 lat;
    F32 lon;
};

struct RoadTag
{
    RoadTag* next;
    String8 key;
    String8 value;
};

enum RoadTagResultEnum
{
    ROAD_TAG_FOUND = 0,
    ROAD_TAG_NOT_FOUND = 1,
};

struct RoadTagResult
{
    RoadTagResultEnum result;
    String8 value;
};

struct RoadNodeSlot
{
    RoadNode* first;
    RoadNode* last;
};

struct RoadWay
{
    RoadWay* next;

    String8 type;
    U64 id;

    U64* node_ids; // fixed array with node_count lenght and node ids as index
    U64 node_count;

    RoadTag* tags; // linked list
    U64 tag_count;

    // tag info
    F32 road_width;
};

struct RoadVertex
{
    Vec2F32 pos;
    Vec2F32 uv;
};

struct RoadWayListElement
{
    RoadWayListElement* next;
    RoadWay* road_way;
};

struct RoadWayQueue
{
    RoadWayListElement* first;
    RoadWayListElement* last;
};

struct NodeUtm
{
    NodeUtm* next;
    U64 id;
    union
    {
        Vec2F32 pos;
        glm::vec2 vec;
    };
    String8 utm_zone;

    RoadWayQueue roadway_queue; // Linked list of RoadWays sharing this node
};

struct NodeUtmSlot
{
    NodeUtm* first;
    NodeUtm* last;
};

struct Road
{
    Arena* arena;

    /////////////////////////////
    String8 openapi_data_cache_path;
    // raw data OpenAPI data
    F32 road_height;
    F32 default_road_width;
    F32 texture_scale;

    RoadNodeSlot* nodes; // hash map
    U64 node_slot_count;
    U64 unique_node_count;

    U64 way_count;
    RoadWay* ways;
    ////////////////////////////////
    // UTM coordinates
    U64 node_hashmap_size;
    Buffer<NodeUtmSlot> node_hashmap; // key is the node id
    Vec2F64 utm_center_offset;        // used for centering utm coordinate based on bounding box
    ////////////////////////////////
    // Graphics API
    wrapper::Road* w_road;
    glm::mat4 model_matrix;
    Buffer<RoadVertex> vertex_buffer;
    /////////////////////////
};

enum RoadDirection
{
    RoadDirection_From,
    RoadDirection_To,
    RoadDirection_Count
};

struct AdjacentNodeLL
{
    AdjacentNodeLL* next;
    NodeUtm* node;
};

struct RoadCrossSection
{
    Vec2F32 top;
    Vec2F32 btm;
    NodeUtm* node;
};
struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct CarVertex
{
    Vec3F32 pos;
    F32 uv[2];
};

struct Car
{
    glm::vec3 cur_pos;
    NodeUtm* source;
    NodeUtm* target;
    glm::vec3 dir;
    F32 speed; //
};

struct CarSim
{
    Arena* arena;

    Buffer<Car> cars;

    Buffer<city::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;
    wrapper::CgltfSampler sampler;
    Rng1F32 car_center_offset;
    // Graphics API
    wrapper::Car* car;
};

struct CarInstance
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
};
// ~mgj: Globals
read_only static RoadNode g_road_node = {&g_road_node, 0, 0.0f, 0.0f};
read_only static NodeUtm g_road_node_utm = {&g_road_node_utm, 0, 0.0f, 0.0f};
///////////////////////
static Road*
RoadCreate(wrapper::VulkanContext* vk_ctx, String8 cache_path);
static void
RoadDestroy(wrapper::VulkanContext* vk_ctx, Road* road);
static void
RoadsBuild(Road* road);
static RoadTagResult
RoadTagFind(Arena* arena, Buffer<RoadTag> tags, String8 tag_to_find);
static inline RoadNode*
NodeFind(Road* road, U64 node_id);
static NodeUtm*
NodeUtmFind(Road* road, U64 node_id);
static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<RoadVertex> buffer, U32* cur_idx);
static void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, RoadWay* current_road_way);
// ~mgj: Cars
static CarSim*
CarSimCreate(wrapper::VulkanContext* vk_ctx, U32 car_count, Road* road);
static void
CarSimDestroy(wrapper::VulkanContext* vk_ctx, CarSim* car_sim);
static Buffer<CarInstance>
CarUpdate(Arena* arena, CarSim* car, Road* road, F32 time_delta);
} // namespace city
