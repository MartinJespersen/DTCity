#include "../shader_shared.h"

bool is_face_inside_road_segment(vec2 road_corner_pos[road_corner_count], vec2 face_avg)
{
    bool all_positive = true;
    bool all_negative = true;
    for (int i = 0; i < road_corner_count; i++) {
        vec2 edge = road_corner_pos[(i + 1) % road_corner_count] - road_corner_pos[i];
        vec2 to_point = face_avg - road_corner_pos[i];
        float cross_z = edge.x * to_point.y - edge.y * to_point.x;
        if (cross_z <= 0.0) all_positive = false;
        if (cross_z >= 0.0) all_negative = false;
        if (!all_positive && !all_negative) return false;
    }
    return true;
}

bool in_bounds(Bounds bounds, vec2 pos)
{
    return pos.x >= bounds.min.x && pos.x <= bounds.max.x &&
        pos.y >= bounds.min.y && pos.y <= bounds.max.y;
}

bool triangle_bounds_overlap(Bounds bounds, vec2 triangle[3])
{
    vec2 triangle_min = min(triangle[0], min(triangle[1], triangle[2]));
    vec2 triangle_max = max(triangle[0], max(triangle[1], triangle[2]));

    return triangle_max.x >= bounds.min.x && triangle_min.x <= bounds.max.x &&
        triangle_max.y >= bounds.min.y && triangle_min.y <= bounds.max.y;
}
