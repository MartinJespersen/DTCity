#pragma once

namespace city
{

struct BuildingVertex
{
    Vec3F32 pos;
    Vec2F32 uv;
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

enum TagResultEnum
{
    ROAD_TAG_FOUND = 0,
    ROAD_TAG_NOT_FOUND = 1,
};

struct TagResult
{
    TagResultEnum result;
    String8 value;
};

struct RoadNodeSlot
{
    RoadNode* first;
    RoadNode* last;
};

struct Way
{
    Way* next;

    U64 id;

    U64* node_ids; // fixed array with node_count lenght and node ids as index
    U64 node_count;

    Buffer<Tag> tags;
};

struct RoadVertex
{
    Vec2F32 pos;
    Vec2F32 uv;
};

struct WayListElement
{
    WayListElement* next;
    Way* road_way;
};

struct WayQueue
{
    WayListElement* first;
    WayListElement* last;
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

    WayQueue way_queue; // Linked list of RoadWays sharing this node
};

struct NodeUtmSlot
{
    NodeUtm* first;
    NodeUtm* last;
};

struct WayBuilding
{
    Buffer<Way> way_buffer;
};

struct NodeWays
{
    RoadNodeSlot* nodes;
    U64 node_slot_count;
    U64 unique_node_count;

    Buffer<Way> ways;
};

struct NodeUtmStructure
{
    U64 node_hashmap_size;
    Buffer<NodeUtmSlot> node_hashmap; // key is the node id
    Vec2F64 utm_center_offset;        // used for centering utm coordinate based on bounding box
};

union GCSBoundingBox
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

struct Road
{
    Arena* arena;

    /////////////////////////////
    String8 openapi_data_cache_path;
    // raw data OpenAPI data
    F32 road_height;
    F32 default_road_width;
    F32 texture_scale;

    NodeWays node_ways;
    ////////////////////////////////
    // UTM coordinates
    NodeUtmStructure node_utm_structure;
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
    SamplerInfo sampler_info;
    Rng1F32 car_center_offset;
    // Graphics API
    wrapper::Car* car;
};

struct Buildings
{
    Arena* arena;
    String8 data_cache_path;

    NodeWays node_ways;
    NodeUtmStructure node_utm_structure;

    // wrapper::AssetId texture_asset_id;
    wrapper::AssetId vertex_buffer_id;
    wrapper::AssetId index_buffer_id;

    Buffer<city::BuildingVertex> vertex_buffer;
    Buffer<U32> index_buffer;
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
RoadCreate(wrapper::VulkanContext* vk_ctx, String8 cache_path, GCSBoundingBox* gcs_bbox);
static void
RoadDestroy(wrapper::VulkanContext* vk_ctx, Road* road);
static String8
DataFetch(Arena* arena, String8 data_cache_path, String8 query, GCSBoundingBox* gcs_bbox);
static Buffer<RoadVertex>
RoadVertexBufferCreate(Road* road);
static void
NodeStructureCreate(Arena* arena, NodeWays* node_ways, GCSBoundingBox* utm_bbox,
                    U64 hashmap_slot_count, NodeUtmStructure* out_node_utm_structure);
static TagResult
TagFind(Arena* arena, Buffer<Tag> tags, String8 tag_to_find);
static inline RoadNode*
NodeFind(NodeWays* road, U64 node_id);
static NodeUtm*
UtmNodeFind(NodeUtmStructure* road, U64 node_id);
static NodeUtm*
RandomUtmNodeFind(NodeUtmStructure* utm_node_structure);
static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<RoadVertex> buffer, U32* cur_idx);
static void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, Way* current_road_way);
// ~mgj: Cars
static CarSim*
CarSimCreate(wrapper::VulkanContext* vk_ctx, U32 car_count, Road* road);
static void
CarSimDestroy(wrapper::VulkanContext* vk_ctx, CarSim* car_sim);
static Buffer<CarInstance>
CarUpdate(Arena* arena, CarSim* car, Road* road, F32 time_delta);

// ~mgj: Buildings
static Buildings*
BuildingsCreate(String8 cache_path, F32 road_height, GCSBoundingBox* gcs_bbox);
static void
BuildingDestroy(Buildings* building);
static void
BuildingsBuffersCreate(Arena* arena, city::Buildings* buildings, F32 road_height,
                       Buffer<city::BuildingVertex>* out_vertex_buffer,
                       Buffer<U32>* out_index_buffer);

} // namespace city
