#pragma once

#include "shaders/shader_shared.h"

#include "render/render.hpp"
#include "io.hpp"

namespace ui
{

enum PlaneType
{
    PlaneType_Left,
    PlaneType_Right,
    PlaneType_Top,
    PlaneType_Btm,
    PlaneType_Back,
    PlaneType_Front,
    PlaneType_Count
};

static_assert(PlaneType_Count == shader::frustum_plane_count);
using Frustum = shader::Frustum;

using CameraUniformBuffer = shader::CameraUniformBuffer;

struct Camera
{
    render::MappedHandle<CameraUniformBuffer> mut_handles;
    ui::Frustum frustum_planes;
    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
    glm::vec3 position;
    glm::vec3 view_dir;
    Vec2F64 mouse_pos_last;
    F32 move_sensitivity;
    F32 fov;
    F32 yaw;
    F32 pitch;
    Vec2U32 cur_framebuffer_extent;
};

void
camera_init(Arena* arena, Camera* camera);
void
camera_update(Camera* camera, io::IO* input, F64 time, Vec2S32 extent, bool enable);

g_internal void
_camera_uniform_buffer_update(ui::Camera* camera, render::MappedHandle<CameraUniformBuffer> mut_handle, Vec2U32 screen_res);
bool
frustum_check_from_bounding_box(Frustum* frustum, Rng3F32 bbox);

g_internal void
_frustum_planes_calculate(Frustum* out_frustum, const glm::mat4 matrix);
bool
is_bounding_sphere_to_be_culled(Camera& camera, Rng3F32& bbox);
} // namespace ui
