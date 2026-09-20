#pragma once

#include "generated/colormaps.h"

namespace city
{

// /////////////////////////////////
static_assert(sizeof(RoadSegmentCorners) == 64, "size of road segment might not match shader size");

struct Road
{
    Arena* arena;

    // Input
    Rng2F64 bbox;
    String8 netascore_file_path;

    /////////////////////////////
    // raw data OpenAPI data
    F32 road_height;
    F32 default_road_width;
    glm::dmat4 ecef_to_local;
    Map<osm::EdgeId, RoadInfo>* road_info_map;

    ArrayResourcePoolHandle bvh_handle; // NOTE: cannot be replaced at the moment

    ////////////////////////////////
    // Graphics API

    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    render::Handle texture_handle;
    render::Handle colormap_handle;
    U32 current_handle_idx;
    bool new_vertex_handle_loading;
    RoadOverlayOption overlay_option_cur;
    render::Handle segment_buffer_handle;
    render::Handle segment_node_buffer_handle;
    /////////////////////////
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

struct BuildingRenderInfo
{
    Buffer<render::TileVertex> vertex_buffer;
    Buffer<U32> index_buffer;
    U32 roof_index_offset;
    U32 roof_index_count;

    U32 facade_index_offset;
    U32 facade_index_count;
};

struct Buildings
{
    String8 cache_file_name;

    String8 roof_texture_path;
    String8 facade_texture_path;

    render::TilePipelineData roof_model_handles;
    render::TilePipelineData facade_model_handles;
};

enum class AsyncTaskType : U32
{
    None,
    Neta,
    Osm,
    Road,
    CarSim,
    Cached,
};

struct RoadBuildTask
{
    Road* road;
    osm::Network* network;
    Bvh* bvh;
};

struct AsyncCityTask
{
    AsyncCityTask* next;
    AsyncCityTask* prev;

    AsyncTaskType type;
    union
    {
        async::AsyncTaskStatus<neta::NetaTaskState>* neta;
        async::AsyncTaskStatus<osm::Network>* osm;
        async::AsyncTaskStatus<RoadBuildTask>* road;
        async::AsyncTaskStatus<city::AgentSim>* agent_sim;
        AsyncTaskType cached_type;
    };
};

struct AsyncCityTaskList
{
    AsyncCityTask* first;
    AsyncCityTask* last;
};

enum class AreaCoordinateType
{
    Wgs84,
    Utm
};

enum class AreaTilesetSource
{
    Terrain, // Zero initialization selects default ion terrain.
    Path,
    IonAsset
};

struct AreaConfig
{
    String8 name;
    AreaCoordinateType coordinate_type;
    Vec2F64 wgs84; // Longitude (x), latitude (y), in degrees.
    util::UtmCoordinate utm;
    F32 bbox_width_meters;
    F32 bbox_height_meters;
    AreaTilesetSource tileset_source;
    String8 tileset_path;
    S64 tileset_ion_asset_id;
    bool bbox_clipping_enabled;
    bool custom_geometry_enabled;
};

g_internal Vec2F64
city_area_wgs84_get(const AreaConfig* config);

struct City
{
    Arena* arena;

    String8 tileset_url;
    Rng2F64 bbox;
    String8 cache_path;
    osm::Network* osm_network;
    bool neta_task_done;
    bool osm_task_done;
    bool road_building_started;
    bool road_building_done;
    bool agent_creation_started;
    bool cars_creation_done;

    bool no_gui_focus;

    Road road;
    AgentSim car_sim;
    F32 all_agent_scale_factor;
    Buildings buildings;
    ArrayResourcePoolHandle tileset_handle;
    neta::NetaState* neta_state;
    ResourcePoolHandle camera_handle;

    // async
    AsyncCityTaskList task_list;
};

// global

g_internal void
road_destroy(Road* road);
g_internal city::RoadBuildResult
road_segment_build(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> edge_buffer, F32 default_road_width,
                   F32 road_height, glm::dmat4& ecef_to_local, Map<osm::EdgeId, RoadInfo>* road_info_map);

g_internal AsyncCityTask*
_cache_and_parse_osm_json(async::ThreadPool* thread_pool, Road* road, osm::Network* osm_network);
g_internal void
quad_to_buffer_add(RoadSegmentCorners* road_segment, Buffer<render::Vertex3DBlend> buffer, Buffer<U32> indices,
                   U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
// ~mgj: Buildings
g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, Rng2F64 bbox);
g_internal void
buildings_build(City* city, osm::Network* osm_network, render::SamplerInfo* sampler_info, glm::dmat4& ecef_to_local,
                F32 road_height);
g_internal void
building_destroy(City* city);
g_internal void
buildings_buffers_create(Arena* arena, osm::Network* network, F32 road_height, glm::dmat4& ecef_to_local,
                         BuildingRenderInfo* out_render_info);
g_internal Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F64> node_buffer);

// ~mgj: HTTP and caching
g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox);
g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler);
g_internal async::AsyncTaskContinuation<RoadBuildTask>
road_build(async::ThreadInfo info, async::AsyncTaskStatus<RoadBuildTask>* status);
g_internal async::AsyncTaskContinuation<AgentSim>
agent_sim_build(async::ThreadInfo info, async::AsyncTaskStatus<AgentSim>* status);
g_internal void
road_create(City* city, Road* in_out_road, glm::dmat4& ecef_to_local, String8 area, String8 bbox_cache_str);
g_internal Map<osm::EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> road_edge_buf,
                       Map<S64, neta::EdgeList>* neta_edge_map);
g_internal void
city_build(City* city, Rng2F64 bbox, String8 tileset_url, String8 area);
g_internal void
city_area_streaming_begin(City* city, const AreaConfig* area_config);
g_internal void
city_area_streaming_end(City* city);
g_internal void
city_update(City* city, Buffer<city::Coordinate> new_agent_coords, async::ThreadPool* thread_pool,
            RoadOverlayOption neta_overlay_option, Vec2U32 framebuffer_dim, const AreaConfig* city_config,
            render::MeshHandle hover_icon_mesh_handle);
g_internal void
city_init(City* city, String8 cache_path);
g_internal void
city_release(City* city);

g_internal neta::Edge*
edge_from_road_edge(osm::Network* network, osm::RoadEdge* road_edge, Map<S64, neta::EdgeList>* edge_list_map);

g_internal Buffer<render::TileVertex>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer);

g_internal void
road_segment_from_road_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1,
                             F32 road_width);
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
g_internal void
bvh_create(Bvh* bvh, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max);
////////////////////////////////////

g_internal Vec3F64
height_dim_add(Vec2F64 pos, F64 height);
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

// coordinates from str list

g_internal Rng3F32
_agent_world_bounds_from_transform(Rng3F32 model_bounds, glm::mat4 model_transform);

// helpers
} // namespace city
