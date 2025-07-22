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

struct RoadTags
{
    RoadTags* next;
    String8 key;
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

    RoadTags* tags;
    U64 tag_count;
};

struct RoadVertex
{
    glm::vec2 pos;
};

struct Road
{
    F32 road_height;
    F32 road_width;

    U64 node_slot_count;
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
CityInit(wrapper::VulkanContext* vk_ctx, City* city, String8 cwd);
static void
CityUpdate(City* city, wrapper::VulkanContext* vk_ctx, U32 image_index);
static void
CityCleanup(City* city);

static void
RoadsBuild(Arena* arena, City* road);

static inline RoadNode*
NodeFind(Road* road, U64 node_id);

} // namespace city
