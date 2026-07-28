#pragma once

namespace geometry
{

struct Triangle2d
{
    glm::vec2 v[3];
};

struct Quad2d
{
    glm::vec2 v[4];
};

struct PolygonMesh2d
{
    Buffer<glm::vec2> vertices;
    Buffer<U32> indices;
};

struct ClassifiedTriangle2d
{
    Triangle2d triangle;
    // Zero is the remaining terrain. A nonzero value is the one-based index
    // of the clipping quad that claimed the triangle.
    U32 region_idx;
};

// NOTE:: all polygon return have counter-clockwise winding
struct ClipResult
{
    // clipping region left after clipping
    Buffer<glm::vec2> inner;

    // all polygons not part of clipping region
    Buffer<Buffer<glm::vec2>> outer;
};

enum class PlaneClassification
{
    Behind,
    InFrontOf,
    On,
};

enum class PolygonPlaneClassification
{
    InFrontOf,
    Behind,
    CoPlanar,
    Straddling
};

struct Plane2d
{
    glm::vec2 n;
    F32 d;
};

glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch);

bool
line_intersection(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2* i);

glm::vec3
barycentric_2d(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c);

bool
intersect_segment_plane(glm::vec2 a, glm::vec2 b, Plane2d p, float& t, glm::vec2& q);

Plane2d
line_from_points(glm::vec2 a, glm::vec2 b);

PlaneClassification
classify_point_on_plane(glm::vec2& p, Plane2d& line);

PolygonPlaneClassification
classify_polygon_to_plane(glm::vec2* poly, U32 poly_vert_count, Plane2d& plane);

void
split_polygon(Arena* arena, glm::vec2* poly, U32 poly_vert_count, Plane2d& plane, Buffer<glm::vec2>* front_poly, Buffer<glm::vec2>* back_poly);

ClipResult
subject_to_triangle_clipping(Arena* arena, Triangle2d& clip_triangle, glm::vec2* subject_poly, U32 subject_poly_side_count);

ClipResult
quad_to_triangle_clipping(Arena* arena, Triangle2d& triangle, Quad2d& quad);

Buffer<glm::vec2>
near_duplicate_vertices_discard_inplace(Buffer<glm::vec2> poly);

PolygonMesh2d
polygon_triangulate(Arena* arena, Buffer<glm::vec2> poly);

Buffer<ClassifiedTriangle2d>
triangle_partition_by_quads(Arena* arena, Triangle2d triangle, Buffer<Quad2d> clipping_quads);

g_internal F32
tolerance_calculation(F32* v, U32 v_count, F32 relative_tolerance = 4.0f * std::numeric_limits<F32>::epsilon(), F32 absolute_tolerance = 0.001f);

g_internal F32
tolerance_calculation(std::initializer_list<F32> scale_values, F32 relative_tolerance = 4.0f * std::numeric_limits<F32>::epsilon(), F32 absolute_tolerance = 0.001f);
} // namespace geometry
