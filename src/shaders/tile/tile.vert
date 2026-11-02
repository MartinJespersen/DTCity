#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "../shader_shared.h"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_overlay_uv;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

layout(push_constant, std430) uniform Constants
{
    TilePipelinePushConstants push_constants;
};

void main() {
    vec3 pos = in_position;
    pos.z -= push_constants.height_offset;
    out_uv = in_uv;
    out_overlay_uv = in_overlay_uv;

    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(pos, 1.0);
}
