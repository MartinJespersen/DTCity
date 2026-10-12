#version 450
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 2) in vec4 row0;
layout(location = 3) in vec4 row1;
layout(location = 4) in vec4 row2;
layout(location = 5) in vec4 row3;

layout(location = 0) out vec2 out_uv;

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

void main() {
    mat4 model = mat4(row0, row1, row2, row3);
    gl_Position = camera_ubo.proj * camera_ubo.view * model * vec4(in_position, 1.0);
    out_uv = in_uv;
}
