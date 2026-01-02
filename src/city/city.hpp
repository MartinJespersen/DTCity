#pragma once

namespace city
{

struct RenderBuffers
{
    Buffer<r_Vertex3D> vertices;
    Buffer<U32> indices;
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
    Map<S64, RoadEdge*> edge_map;
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
    Map<S64, RoadEdge*> edge_map;

    ////////////////////////////////
    // Graphics API
    String8 texture_path;
    r_Model3DPipelineData handles;
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

    r_BufferInfo vertex_buffer;
    r_BufferInfo index_buffer;

    r_SamplerInfo sampler_info;
    Rng1F32 car_center_offset;

    r_Handle texture_handle;
    r_Handle vertex_handle;
    r_Handle index_handle;

    String8 texture_path;
};

struct BuildingRenderInfo
{
    Buffer<r_Vertex3D> vertex_buffer;
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

    r_Model3DPipelineData roof_model_handles;
    r_Model3DPipelineData facade_model_handles;
};

g_internal void
road_destroy(Road* road);
g_internal city::RenderBuffers
road_render_buffers_create(Arena* arena, Buffer<city::RoadEdge> edge_buffer, F32 default_road_width,
                           F32 road_height);

g_internal void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<r_Vertex3D> buffer, Buffer<U32> indices,
                U64 way_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way,
                           osm::Network* node_utm_structure);
// ~mgj: Cars
g_internal CarSim*
car_sim_create(String8 asset_path, String8 texture_path, U32 car_count, Road* road);
g_internal void
car_sim_destroy(CarSim* car_sim);
g_internal Buffer<r_Model3DInstance>
car_sim_update(Arena* arena, CarSim* car, F64 time_delta);

// ~mgj: Buildings
g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, F32 road_height, Rng2F64 bbox,
                 r_SamplerInfo* sampler_info);
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
city_cache_read(Arena* arena, String8 cache_file, String8 cache_meta_file, String8 hash_input);
g_internal void
city_cache_write(String8 cache_file, String8 cache_meta_file, String8 content,
                 String8 hash_content);

g_internal U64
hash_u64_from_str8(String8 str);
g_internal B32
cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
g_internal B32
cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox);
g_internal r_Model3DPipelineDataList
land_create(Arena* arena, String8 glb_path);
g_internal void
land_destroy(r_Model3DPipelineDataList list);
g_internal r_SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler);
g_internal Road*
road_create(String8 texture_path, String8 cache_path, Rng2F64 bbox, r_SamplerInfo* sampler_info);

g_internal EdgeStructure
road_edge_structure_create(Arena* arena);

g_internal neta_Edge*
neta_edge_from_road_edge(RoadEdge* road_edge, Map<S64, neta_EdgeList>* edge_list_map);

g_internal Buffer<r_Vertex3D>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer);

// privates ///////////////////////////////////////////////////

g_internal Vec2F32
world_position_offset_adjust(Vec2F32 position);
g_internal osm::UtmLocation
random_utm_road_node_get();
g_internal osm::UtmLocation
utm_location_find(U64 node_id);
} // namespace city
