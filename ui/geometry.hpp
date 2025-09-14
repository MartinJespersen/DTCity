#pragma once

namespace ui
{
struct Camera
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

static void
CameraInit(Camera* camera);

static void
CameraUpdate(Camera* camera, IO* input, DT_Time* time, VkExtent2D extent);

static B32
LineIntersect(F64 x1, F64 y1, F64 x2, F64 y2, F64 x3, F64 y3, F64 x4, F64 y4, F64* x, F64* y);
static B32
LineIntersect2F32(Vec2F32 v0, Vec2F32 v1, Vec2F32 v2, Vec2F32 v3, Vec2F32* res);

} // namespace ui
