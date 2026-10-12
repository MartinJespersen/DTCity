#version 450
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

void main()
{
    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(in_position, 1.0);
    out_color = in_color;
}
