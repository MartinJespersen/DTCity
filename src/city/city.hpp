#pragma once

struct city_Road
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
    glm::mat4 model_matrix;

    String8 texture_path;
    r_Model3DPipelineData handles;

    Buffer<r_Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
    /////////////////////////
};
namespace city
{

struct AdjacentNodeLL
{
    AdjacentNodeLL* next;
    osm_UtmNode* node;
};

struct RoadCrossSection
{
    Vec2F32 top;
    Vec2F32 btm;
    osm_UtmNode* node;
};
struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct Car
{
    glm::vec3 cur_pos;
    osm_UtmNode* source;
    osm_UtmNode* target;
    glm::vec3 dir;
    F32 speed; //
};

struct CarSim
{
    Arena* arena;

    Buffer<Car> cars;

    R_BufferInfo vertex_buffer;
    R_BufferInfo index_buffer;

    R_SamplerInfo sampler_info;
    Rng1F32 car_center_offset;

    R_Handle texture_handle;
    R_Handle vertex_handle;
    R_Handle index_handle;

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

struct Model3DInstance
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
};

static void
RoadDestroy(city_Road* road);
static void
RoadVertexBufferCreate(city_Road* road, Buffer<r_Vertex3D>* out_vertex_buffer,
                       Buffer<U32>* out_index_buffer, osm_Network* node_utm_structure);

static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<r_Vertex3D> buffer, Buffer<U32> indices,
                U64 way_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
static void
RoadIntersectionPointsFind(city_Road* road, RoadSegment* in_out_segment, osm_Way* current_road_way,
                           osm_Network* node_utm_structure);
// ~mgj: Cars
static CarSim*
CarSimCreate(String8 asset_path, String8 texture_path, U32 car_count, city_Road* road);
static void
CarSimDestroy(CarSim* car_sim);
static Buffer<Model3DInstance>
CarUpdate(Arena* arena, CarSim* car, F32 time_delta);

// ~mgj: Buildings
static Buildings*
BuildingsCreate(String8 cache_path, String8 texture_path, F32 road_height,
                osm_BoundingBox* gcs_bbox, R_SamplerInfo* sampler_info,
                osm_Network* node_utm_structure);
static void
BuildingDestroy(Buildings* building);
static void
BuildingsBuffersCreate(Arena* arena, F32 road_height, BuildingRenderInfo* out_render_info,
                       osm_Network* node_utm_structure);
static Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F32> node_buffer);

static Rng2F32
UtmFromBoundingBox(osm_BoundingBox bbox);

// ~mgj: HTTP and caching
static String8
city_http_call_wrapper(Arena* arena, String8 query_str, HTTP_RequestParams* params);
static Result<String8>
city_cache_read(Arena* arena, String8 cache_file, String8 cache_meta_file, String8 hash_input);
static void
city_cache_write(String8 cache_file, String8 cache_meta_file, String8 content,
                 String8 hash_content);

static U64
HashU64FromStr8(String8 str);
static B32
city_cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
static B32
city_cache_needs_update(String8 data_file_str, String8 cache_meta_file_path);
} // namespace city

static String8
city_str8_from_wqs_coord(Arena* arena, osm_BoundingBox* bbox);
g_internal r_Model3DPipelineDataList
city_land_create(Arena* arena, String8 glb_path);
g_internal void
city_land_destroy(r_Model3DPipelineDataList list);
static R_SamplerInfo
city_sampler_from_cgltf_sampler(gltfw_Sampler sampler);
static city_Road*
city_road_create(String8 texture_path, String8 cache_path, osm_BoundingBox* gcs_bbox,
                 R_SamplerInfo* sampler_info, osm_Network* node_utm_structure);
