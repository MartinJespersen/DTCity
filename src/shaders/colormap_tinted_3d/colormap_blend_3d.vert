#version 450
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uvec2 in_object_id;
layout(location = 3) in vec2 in_colormap;

layout(location = 0) out vec2 out_uv;
layout(location = 1) flat out uvec2 out_object_id;
layout(location = 2) flat out vec2 out_colormap;

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

void main() {
    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_object_id = in_object_id;
    out_colormap = in_colormap;
}
