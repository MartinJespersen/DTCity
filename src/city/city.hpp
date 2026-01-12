#pragma once

namespace city
{

struct RenderBuffers
{
    Buffer<render::Vertex3D> vertices;
    Buffer<U32> indices;
};

typedef S64 EdgeId;

#define ROAD_OVERLAY_RED {1.0f, 0.0f, 0.0f}
#define ROAD_OVERLAY_GREEN {0.0f, 1.0f, 0.0f}

#define ROAD_OVERLAY_OPTIONS                                                                       \
    X(None, "None", {})                                                                            \
    X(Bikeability_ft, "Bikeability_ft", ROAD_OVERLAY_RED)                                          \
    X(Bikeability_tf, "Bikeability_tf", ROAD_OVERLAY_RED)                                          \
    X(Walkability_tf, "Walkability_tf", ROAD_OVERLAY_GREEN)                                        \
    X(Walkability_ft, "Walkability_ft", ROAD_OVERLAY_GREEN)

enum class RoadOverlayOption : U32
{
#define X(name, str, color) name,
    ROAD_OVERLAY_OPTIONS
#undef X
        Count
};

read_only g_internal const char* road_overlay_option_strs[] = {
#define X(name, str, color) str,
    ROAD_OVERLAY_OPTIONS
#undef X
};

read_only g_internal Vec3F32 road_overlay_option_colors[] = {
#define X(name, str, color) color,
    ROAD_OVERLAY_OPTIONS
#undef X
};

struct RoadInfo
{
    F32 options[(U32)RoadOverlayOption::Count];
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
    RenderBuffers render_buffers;

    ////////////////////////////////
    // Graphics API
    String8 texture_path;
    render::Model3DPipelineData handles[2];
    U32 current_handle_idx;
    bool new_vertex_handle_loading;
    RoadOverlayOption overlay_option_cur;
    /////////////////////////
};

struct AdjacentNodeLL
{
    AdjacentNodeLL* next;
    osm::Node* node;
};

struct RoadCrossSection
{
    Vec2F32 top;
    Vec2F32 btm;
    osm::UtmLocation node;
};

struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct Car
{
    Vec3F32 cur_pos;
    osm::UtmLocation source_loc;
    osm::UtmLocation target_loc;
    Vec3F32 dir;
    F32 speed;
};

struct CarSim
{
    Arena* arena;

    Buffer<Car> cars;

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
g_internal city::RenderBuffers
road_render_buffers_create(Arena* arena, Buffer<city::RoadEdge> edge_buffer, F32 default_road_width,
                           F32 road_height);

g_internal void
quad_to_buffer_add(RoadSegment* road_segment, Buffer<render::Vertex3D> buffer, Buffer<U32> indices,
                   U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way,
                           osm::Network* node_utm_structure);
// ~mgj: Cars
g_internal CarSim*
car_sim_create(String8 asset_path, String8 texture_path, U32 car_count, Road* road);
g_internal void
car_sim_destroy(CarSim* car_sim);
g_internal Buffer<render::Model3DInstance>
car_sim_update(Arena* arena, CarSim* car, F64 time_delta);

// ~mgj: Buildings
g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, F32 road_height, Rng2F64 bbox,
                 render::SamplerInfo* sampler_info);
g_internal void
building_destroy(Buildings* building);
g_internal void
buildings_buffers_create(Arena* arena, F32 road_height, BuildingRenderInfo* out_render_info);
g_internal Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F32> node_buffer);

// ~mgj: HTTP and caching
g_internal String8
city_http_call_wrapper(Arena* arena, String8 query_str, HTTP_RequestParams* params);
g_internal Result<String8>
cache_read(Arena* arena, String8 cache_file, String8 cache_meta_file, String8 hash_input);
g_internal void
cache_write(String8 cache_file, String8 cache_meta_file, String8 content, String8 hash_content);

g_internal U64
hash_u64_from_str8(String8 str);
g_internal B32
cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
g_internal B32
cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox);
g_internal render::Model3DPipelineDataList
land_create(Arena* arena, String8 glb_path);
g_internal void
land_destroy(render::Model3DPipelineDataList list);
g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler);
g_internal Road*
road_create(String8 texture_path, String8 cache_path, String8 data_dir, Rng2F64 bbox,
            Rng2F64 utm_coords, render::SamplerInfo* sampler_info);
g_internal void
road_vertex_buffer_switch(Road* road, RoadOverlayOption overlay_option);
g_internal EdgeStructure
road_edge_structure_create(Arena* arena);
g_internal Map<EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, Buffer<RoadEdge> road_edge_buf,
                       Map<S64, neta_EdgeList>* neta_edge_map);

g_internal neta_Edge*
neta_edge_from_road_edge(RoadEdge* road_edge, Map<S64, neta_EdgeList>* edge_list_map);

g_internal Buffer<render::Vertex3D>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer);

// privates ///////////////////////////////////////////////////

g_internal Vec2F32
world_position_offset_adjust(Vec2F32 position);
g_internal osm::UtmLocation
random_utm_road_node_get();
g_internal osm::UtmLocation
utm_location_find(U64 node_id);
} // namespace city
