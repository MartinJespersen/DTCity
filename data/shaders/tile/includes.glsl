const uint CORNERS_COUNT = 4;
const uint OPTION_COUNT = 5;

struct RenderPushConstants
{
    uint base_tex;
    uint overlay_tex_idx;
    uint overlay_enabled;
    uint64_t colormap_address;
    uint colormap_len;
    float overlay_translation_x;
    float overlay_translation_y;
    float overlay_scale_x;
    float overlay_scale_y;
    float height_offset;
    float lod_fade;
    float tessellation_factor;
    float tessellated_edge_size;
    uint road_test_enabled;
};

struct Bounds
{
    vec2 min;
    vec2 max;
};

struct RoadSegment {
    uvec2 id;
    vec2 positions[CORNERS_COUNT];
    float options[OPTION_COUNT];
};

struct Vertex {
    float pos_x;
    float pos_y;
    float pos_z;
    float overlay_option;
    vec2 uv;
    vec2 overlay_uv;
    uvec2 id;
    uint road_segment_idx;
};

struct Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
};

bool is_face_inside_road_segment(vec2 road_corner_pos[CORNERS_COUNT], vec2 face_avg)
{
    bool all_positive = true;
    bool all_negative = true;
    for (int i = 0; i < CORNERS_COUNT; i++) {
        vec2 edge = road_corner_pos[(i + 1) % CORNERS_COUNT] - road_corner_pos[i];
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
