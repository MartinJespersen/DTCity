#pragma once

#include "generated/colormaps.h"

namespace city
{

typedef S64 EdgeId;
enum RoadSegmentCornerCoord
{
    RoadSegmentCornerCoord_TopLeft,
    RoadSegmentCornerCoord_TopRight,
    RoadSegmentCornerCoord_BottomRight,
    RoadSegmentCornerCoord_BottomLeft,
    RoadSegmentCornerCoord_Count
};

#define ROAD_OVERLAY_OPTIONS                                                                                                                                                                           \
    X(None, "None")                                                                                                                                                                                    \
    X(Bikeability_ft, "Bikeability_ft")                                                                                                                                                                \
    X(Bikeability_tf, "Bikeability_tf")                                                                                                                                                                \
    X(Walkability_tf, "Walkability_tf")                                                                                                                                                                \
    X(Walkability_ft, "Walkability_ft")

enum RoadOverlayOption : U32
{
#define X(name, str) RoadOverlayOption_##name,
    ROAD_OVERLAY_OPTIONS
#undef X
        RoadOverlayOption_Count
};

read_only g_internal const char* road_overlay_option_strs[] = {
#define X(name, str) str,
    ROAD_OVERLAY_OPTIONS
#undef X
};

struct RoadInfo
{
    F32 options[RoadOverlayOption_Count];
};

struct alignas(8) RoadSegmentCorners
{
    EdgeId edge_id;
    Vec2F32 corners[RoadSegmentCornerCoord_Count];
    RoadInfo road_info;
};
// BVH types
enum Bounds : U32
{
    Bounds_Min,
    Bounds_Max,
    Bounds_Count
};

struct RoadSegmentNode
{
    RoadSegmentNode* next;
    RoadSegmentNode* parent;
    U32 final_idx;
    Rng2F32 bounds;
    RoadSegmentNode* children[2];
    U32 split_axis;
    F32 split_value;

    // buffer range
    U32 start_idx;
    U32 end_idx;
};

struct RoadSegmentNodeStorageBuffer
{
    F32 min_x;
    F32 min_y;
    F32 max_x;
    F32 max_y;
    U32 split_axis;
    F32 split_value;
    U32 is_leaf;
    union
    {
        struct
        {
            U32 child_0_idx;
            U32 child_1_idx;
        };
        struct
        {
            U32 start_idx;
            U32 end_idx;
        };
    };
    U32 _pad;
};
static_assert(sizeof(RoadSegmentNodeStorageBuffer) == 40, "RoadSegmentNodeStorageBuffer must match std430 RoadSegmentNode size");

struct BoundingBox
{
    Vec2F32 center;
    Rng2F32 bounds;
    U32 idx;
};

struct BvhContext
{
    RoadSegmentNode* stack;
    RoadSegmentNode* root;

    U32 road_segment_node_count;

    Buffer<RoadSegmentCorners> road_segment_buffer;
    Buffer<BoundingBox> bb_buffer;

    U32 leaf_bb_max;
};

struct BvhResult
{
    Buffer<RoadSegmentCorners> road_segment_buffer_sorted;
    Buffer<RoadSegmentNodeStorageBuffer> node_buffer;
};
// /////////////////////////////////
static_assert(sizeof(RoadSegmentCorners) == 64, "size of road segment might not match shader size");

struct RoadBuildResult
{
    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    BvhResult bvh_result;
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

struct Road
{
    Arena* arena;

    /////////////////////////////
    String8 openapi_data_file_name;
    // raw data OpenAPI data
    F32 road_height;
    F32 default_road_width;
    F32 texture_scale;
    EdgeStructure edge_structure;
    Map<EdgeId, RoadInfo>* road_info_map;
    RoadBuildResult road_build_result;

    ////////////////////////////////
    // Graphics API
    render::Handle zero_colormap_handle;
    render::Handle colormap_handle;
    U32 current_handle_idx;
    bool new_vertex_handle_loading;
    RoadOverlayOption overlay_option_cur;
    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    /////////////////////////
};

struct AdjacentNodeLL
{
    AdjacentNodeLL* next;
    osm::Node* node;
};

struct RoadCrossSection
{
    Vec2F64 top;
    Vec2F64 btm;
    osm::EcefLocation node;
};

struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct Car
{
    Vec3F64 cur_pos_ecef;
    osm::EcefLocation source_loc;
    osm::EcefLocation target_loc;
    Vec3F64 dir;
    F32 speed;
};

struct CarSim
{
    Arena* arena;

    Buffer<Car> cars;
    Buffer<F32> car_height;

    render::BufferInfo vertex_buffer;
    render::BufferInfo index_buffer;

    render::SamplerInfo sampler_info;
    Rng1F32 car_center_offset;

    render::Handle texture_handle;
    render::Handle vertex_handle;
    render::Handle index_handle;

    String8 texture_path;
};

struct BuildingRenderInfo
{
    Buffer<render::Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
    U32 roof_index_offset;
    U32 roof_index_count;

    U32 facade_index_offset;
    U32 facade_index_count;
};

struct Buildings
{
    Arena* arena;
    String8 cache_file_name;

    String8 roof_texture_path;
    String8 facade_texture_path;

    render::Model3DPipelineData roof_model_handles;
    render::Model3DPipelineData facade_model_handles;
};

g_internal void
road_destroy(Road* road);
g_internal city::RoadBuildResult
road_segment_build(Arena* arena, Buffer<city::RoadEdge> edge_buffer, F32 default_road_width, F32 road_height, glm::dmat4& ecef_to_local, Map<EdgeId, RoadInfo>* road_info_map);

g_internal void
quad_to_buffer_add(RoadSegmentCorners* road_segment, Buffer<render::Vertex3DBlend> buffer, Buffer<U32> indices, U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way, osm::Network* node_utm_structure);
// ~mgj: Cars
g_internal CarSim*
car_sim_create(String8 asset_path, String8 texture_path, U32 car_count);
g_internal void
car_sim_destroy(CarSim* car_sim);
g_internal Buffer<render::Model3DInstance>
car_sim_update(Arena* arena, CarSim* car, F64 time_delta, glm::dmat4& ecef_to_local);

// ~mgj: Buildings
g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, Rng2F64 bbox);
g_internal void
buildings_build(Buildings* buildings, render::SamplerInfo* sampler_info, glm::dmat4& ecef_to_local, F32 road_height);
g_internal void
building_destroy(Buildings* building);
g_internal void
buildings_buffers_create(Arena* arena, F32 road_height, glm::dmat4& ecef_to_local, BuildingRenderInfo* out_render_info);
g_internal Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F64> node_buffer);

// ~mgj: HTTP and caching
g_internal String8
city_http_call_wrapper(Arena* arena, String8 query_str, HTTP_RequestParams* params);
g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox);
g_internal render::Model3DPipelineDataList
land_create(Arena* arena, String8 glb_path);
g_internal void
land_destroy(render::Model3DPipelineDataList list);
g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler);
g_internal Road*
road_create(String8 texture_path, String8 cache_path, String8 data_dir, Rng2F64 bbox, Rng2F64 utm_coords, render::SamplerInfo* sampler_info);
g_internal void
road_vertex_buffer_switch(Road* road, RoadOverlayOption overlay_option);
g_internal EdgeStructure
road_edge_structure_create(Arena* arena);
g_internal Map<EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, Buffer<RoadEdge> road_edge_buf, Map<S64, neta::EdgeList>* neta_edge_map);

g_internal neta::Edge*
edge_from_road_edge(RoadEdge* road_edge, Map<S64, neta::EdgeList>* edge_list_map);

g_internal Buffer<render::Vertex3D>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer);

// privates ///////////////////////////////////////////////////

g_internal void
road_segment_from_road_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1, F32 road_width);
g_internal void
road_segments_coalesce(RoadSegment* in_out_road_segment_0, RoadSegment* in_out_road_segment_1, F32 road_width);
g_internal F32
tag_value_get(Arena* arena, String8 key, F32 default_width, Buffer<osm::Tag> tags);

// BVH functions /////////////////////////////
g_internal void
quick_select(Buffer<BoundingBox> center_buffer, U32 split_axis, U32 start_idx, U32 end_idx, U32 k);
g_internal Rng2F32
bounds_union(Rng2F32 a, Rng2F32 b);
g_internal Rng2F32
bounds_union(Rng2F32 rng, Vec2F32 vec);
g_internal Rng2F32
bounds_union(Buffer<BoundingBox> center_buffer, U32 start_idx, U32 end_idx);
g_internal Axis2
split_axis_find(Buffer<BoundingBox> bb_buffer, U32 start_idx, U32 end_idx);
g_internal BvhResult
bvh_create(Arena* arena, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max);
////////////////////////////////////

g_internal Vec3F64
height_dim_add(Vec2F64 pos, F64 height);
g_internal Rng1F32
car_center_height_offset(Buffer<gltfw_Vertex3D> vertices);
g_internal osm::EcefLocation
random_ecef_road_node_get();
g_internal F64
cross_2f64_z_component(Vec2F64 a, Vec2F64 b);
g_internal B32
AreTwoConnectedLineSegmentsCollinear(Vec2F64 prev, Vec2F64 cur, Vec2F64 next);

enum Direction
{
    Direction_Undefined,
    Direction_Clockwise,
    Direction_CounterClockwise
};

g_internal Direction
ClockWiseTest(Buffer<Vec2F64> node_buffer);
g_internal Buffer<U32>
IndexBufferCreate(Arena* arena, U64 buffer_size, Direction direction);
g_internal B32
PointInTriangle(Vec2F64 p1, Vec2F64 p2, Vec2F64 p3, Vec2F64 point);
g_internal void
NodeBufferPrintDebug(Buffer<Vec2F64> node_buffer);
} // namespace city
