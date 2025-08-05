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
    String8 type;
    U64 id;

    U64* node_ids;
    U64 node_count;

    RoadTag* tags;
    U64 tag_count;
};

struct RoadVertex
{
    glm::vec2 pos;
    glm::vec2 uv;
};

struct Road
{
    F32 road_height;
    F32 default_road_width;
    U64 node_slot_count;
    F32 texture_scale;

    RoadNodeSlot* nodes;

    U64 way_count;
    RoadWay* ways;

    glm::mat4 model_matrix;
    Buffer<RoadVertex> vertex_buffer;
};

struct RoadQuadCoord
{
    glm::vec2 pos[4];
};

struct City
{
    Arena* arena;

    Road road;
    wrapper::Road* w_road;
};

static void
CityCreate(Context* ctx, City* city);
static void
CityUpdate(City* city, wrapper::VulkanContext* vk_ctx, U32 image_index, String8 shader_path);
static void
CityDestroy(City* city, wrapper::VulkanContext* vk_ctx);

static void
RoadsBuild(Arena* arena, City* road);
static RoadTagResult
RoadTagFind(Arena* arena, Buffer<RoadTag> tags, String8 tag_to_find);
static inline RoadNode*
NodeFind(Road* road, U64 node_id);

// ~mgj: Cars
} // namespace city
