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
    Vec2F32 pos;
    U64 id;
};

struct Road
{
    U64 node_slot_count;
    RoadNodeSlot* nodes;

    U64 way_count;
    RoadWay* ways;

    Buffer<RoadVertex> vertices;
};

struct City
{
    Arena* arena;

    // roads
    F32 road_height;
    U32 road_width;
    Road road;
    wrapper::Road* w_road;
};

static void
CityInit(City* city);

static void
CityCleanup(City* city);

static void
RoadsBuild(Arena* arena, City* road);

static inline RoadNode*
NodeFind(Road* road, U64 node_id);

} // namespace city
