#pragma once

namespace city
{

struct Vertex3D
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
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
    glm::mat4 model_matrix;

    String8 texture_path;
    R_Handle texture_handle;
    R_Handle vertex_handle;
    R_Handle index_handle;

    Buffer<Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
    /////////////////////////
};

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

    Buffer<city::Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;

    R_SamplerInfo sampler_info;
    Rng1F32 car_center_offset;

    R_Handle texture_handle;
    R_Handle vertex_handle;
    R_Handle index_handle;

    String8 texture_path;
};

struct BuildingRenderInfo
{
    Buffer<city::Vertex3D> vertex_buffer;
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

    U32 roof_index_buffer_offset;
    U32 roof_index_count;
    U32 facade_index_buffer_offset;
    U32 facade_index_count;

    R_Handle vertex_handle;
    R_Handle index_handle;
    R_Handle roof_texture_handle;
    R_Handle facade_texture_handle;
};

struct Model3DInstance
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
};

// ~mgj: File Cache
static U64
HashU64FromStr8(String8 str);
static String8
Str8FromGCSCoordinates(Arena* arena, osm_GCSBoundingBox* bbox);
static B32
CacheNeedsUpdate(String8 data_file_str, String8 cache_meta_file_path);

static Road*
RoadCreate(String8 texture_path, String8 cache_path, osm_GCSBoundingBox* gcs_bbox,
           R_SamplerInfo* sampler_info, osm_Network* node_utm_structure);
static void
RoadDestroy(Road* road);
static String8
DataFetch(Arena* arena, String8 cache_dir, String8 cache_name, String8 query,
          osm_GCSBoundingBox* gcs_bbox);
static void
RoadVertexBufferCreate(Road* road, Buffer<Vertex3D>* out_vertex_buffer,
                       Buffer<U32>* out_index_buffer, osm_Network* node_utm_structure);

static void
QuadToBufferAdd(RoadSegment* road_segment, Buffer<Vertex3D> buffer, Buffer<U32> indices, U64 way_id,
                F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx);
static void
RoadIntersectionPointsFind(Road* road, RoadSegment* in_out_segment, osm_Way* current_road_way,
                           osm_Network* node_utm_structure);
// ~mgj: Cars
static CarSim*
CarSimCreate(String8 asset_path, String8 texture_path, U32 car_count, Road* road,
             osm_Network* node_utm_structure);
static void
CarSimDestroy(CarSim* car_sim);
static Buffer<Model3DInstance>
CarUpdate(Arena* arena, CarSim* car, Road* road, F32 time_delta,
          Buffer<osm_UtmNodeList> utm_node_hashmap);

// ~mgj: Buildings
static Buildings*
BuildingsCreate(String8 cache_path, String8 texture_path, F32 road_height,
                osm_GCSBoundingBox* gcs_bbox, R_SamplerInfo* sampler_info,
                osm_Network* node_utm_structure);
static void
BuildingDestroy(Buildings* building);
static void
BuildingsBuffersCreate(Arena* arena, Buildings* buildings, F32 road_height,
                       BuildingRenderInfo* out_render_info, osm_Network* node_utm_structure);
static Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F32> node_buffer);

static Rng2F32
UtmFromBoundingBox(osm_GCSBoundingBox bbox);
} // namespace city
