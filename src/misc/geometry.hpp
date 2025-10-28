#pragma once

struct ui_Camera
{
    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
    glm::vec3 position;
    glm::vec3 view_dir;
    Vec2F64 mouse_pos_last;
    F32 zoom_sensitivity;
    F32 fov;
    F32 yaw;
    F32 pitch;
};

static B32
ui_line_intersect(F64 x1, F64 y1, F64 x2, F64 y2, F64 x3, F64 y3, F64 x4, F64 y4, F64* x, F64* y);
static B32
ui_line_intersect_2f32(Vec2F32 v0, Vec2F32 v1, Vec2F32 v2, Vec2F32 v3, Vec2F32* res);
glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch);
