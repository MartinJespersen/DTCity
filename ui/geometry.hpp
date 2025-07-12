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
UI_CameraInit(Camera* camera);

static void
UI_CameraUpdate(Camera* camera, IO* input, DT_Time* time, VkExtent2D extent);
} // namespace ui
