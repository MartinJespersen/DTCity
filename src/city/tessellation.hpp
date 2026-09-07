namespace city
{

enum RoadSegmentCornerCoord
{
    RoadSegmentCornerCoord_TopLeft,
    RoadSegmentCornerCoord_TopRight,
    RoadSegmentCornerCoord_BottomRight,
    RoadSegmentCornerCoord_BottomLeft,
    RoadSegmentCornerCoord_Count
};

#define ROAD_OVERLAY_OPTIONS            \
    X(None, "None")                     \
    X(Bikeability_ft, "Bikeability_ft") \
    X(Bikeability_tf, "Bikeability_tf") \
    X(Walkability_tf, "Walkability_tf") \
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
    osm::EdgeId edge_id;
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

struct BvhContext
{
    RoadSegmentNode* stack;
    RoadSegmentNode* root;

    U32 road_segment_node_count;

    Buffer<RoadSegmentCorners> road_segment_buffer;
    Buffer<BoundingBox> bb_buffer;

    U32 leaf_bb_max;
};

struct Bvh
{
    Arena* arena;
    U32 loads_in_flight;
    B32 deletion_requested;

    RoadSegmentNode* root;
    Buffer<RoadSegmentCorners> road_segment_buffer_sorted;
    Buffer<RoadSegmentNodeStorageBuffer> node_buffer;
};

struct RoadBuildResult
{
    render::Handle vertex_buffer_handle;
    render::Handle index_buffer_handle;
    Buffer<RoadSegmentCorners> road_segment_buffer;
};
struct TileVertexFace
{
    render::TileVertex v[3];
};

g_internal render::TileMesh
_tessellate_tile_face_for_roads(Arena* arena, city::RoadSegmentNode* root, Buffer<city::RoadSegmentCorners> road_buffer, TileVertexFace& face);

g_internal bool
tesselate_roads(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, Bvh& bvh_result, render::TileMesh* out_mesh);

g_internal geometry::Quad2d
_to_glm_quad(city::RoadSegmentCorners* road);

g_internal bool
_polygon_intersection_sat(geometry::Quad2d quad, geometry::Triangle2d face);

g_internal bool
_face_bounds_overlap(Rng2F32 bounds, geometry::Triangle2d& face);

g_internal void
_assign_vertex_values(glm::vec2 v, TileVertexFace& face, geometry::Triangle2d& projected_tri, render::TileVertex* out_vertex);

render::TileMesh
_render_mesh_from_2d_mesh(Arena* arena, geometry::PolygonMesh2d& triangulated_poly, TileVertexFace& tri, geometry::Triangle2d& projected_tri);

g_internal void
_mesh_append(Arena* arena, ChunkList<render::TileVertex>* vertices_chunk_list, ChunkList<U32>* indices_chunk_list, render::TileMesh& mesh, U32 road_idx);

} // namespace city
