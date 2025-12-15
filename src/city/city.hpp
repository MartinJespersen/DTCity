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
    S64 id;
    S64 node_id_from;
    S64 node_id_to;
    S64 node_id_next;
    S64 node_id_prev;
    S64 way_id;
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

    ////////////////////////////////
    // Graphics API
    String8 texture_path;
    r_Model3DPipelineData handles;
    /////////////////////////
};

struct AdjacentNodeLL
{
    AdjacentNodeLL* next;
    osm::UtmNode* node;
};

struct RoadCrossSection
{
    Vec2F32 top;
    Vec2F32 btm;
    osm::UtmNode* node;
};
struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct Car
{
    glm::vec3 cur_pos;
    osm::UtmNode* source;
    osm::UtmNode* target;
    glm::vec3 dir;
    F32 speed; //
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
road_render_buffers_create(Road* road, Map<S64, city::RoadEdge*>* edge_map);

g_internal void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<r_Vertex3D> buffer, Buffer<U32> indices,
                U64 way_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
g_internal void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm::Way* current_road_way,
                           osm::Network* node_utm_structure);
// ~mgj: Cars
g_internal CarSim*
CarSimCreate(String8 asset_path, String8 texture_path, U32 car_count, Road* road);
g_internal void
car_sim_destroy(CarSim* car_sim);
g_internal Buffer<r_Model3DInstance>
CarUpdate(Arena* arena, CarSim* car, F32 time_delta);

// ~mgj: Buildings
g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, F32 road_height, Rng2F64 bbox,
                 r_SamplerInfo* sampler_info);
g_internal void
building_destroy(Buildings* building);
g_internal void
BuildingsBuffersCreate(Arena* arena, F32 road_height, BuildingRenderInfo* out_render_info,
                       osm::Network* node_utm_structure);
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
HashU64FromStr8(String8 str);
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

g_internal Map<S64, RoadEdge*>*
road_edge_map_create(Arena* arena);

} // namespace city
