#pragma once

#include "shaders/shader_shared.h"

#include "render/render.hpp"
#include "misc/geometry.hpp"
#include "osm/osm.hpp"

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

static_assert(RoadOverlayOption_Count == shader::road_option_count);
static_assert(RoadSegmentCornerCoord_Count == shader::road_corner_count);

using RoadInfo = shader::RoadInfo;

using RoadSegmentCorners = shader::RoadSegmentCorners;
// BVH types
enum Bounds : U32
{
    Bounds_Min,
    Bounds_Max,
    Bounds_Count
};

struct BoundingBox
{
    Vec2F32 center;
    Rng2F32 bounds;
    U32 idx;
};
struct RoadSegmentNode
{
    RoadSegmentNode* next;
    Rng2F32 bounds;
    RoadSegmentNode* children[2];

    // buffer range
    U32 start_idx;
    U32 end_idx;
};

struct RoadBvhContext
{
    RoadSegmentNode* stack;
    RoadSegmentNode* root;

    Buffer<RoadSegmentCorners> road_segment_buffer;
    Buffer<BoundingBox> bb_buffer;
};

struct RoadBvh
{
    Arena* arena;
    U32 loads_in_flight;
    B32 deletion_requested;

    RoadSegmentNode* root;
    Buffer<RoadSegmentCorners> road_segment_buffer_sorted;
};

struct TileVertexFace
{
    render::TileVertex v[3];
};

g_internal render::TileMesh
_tessellate_tile_face_for_roads(Arena* arena, city::RoadSegmentNode* root, Buffer<city::RoadSegmentCorners> road_buffer,
                                Buffer<city::RoadSegmentNode*> node_stack, TileVertexFace& face);

g_internal bool
tesselate_roads(Arena* arena, Buffer<render::TileVertex> vertices, Buffer<U32> indices, RoadBvh& bvh_result, render::TileMesh* out_mesh);

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
