#version 450
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) in vec3 in_instance_position;
layout(location = 1) in vec3 in_position;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec3 out_normal;

layout(push_constant, std430) uniform PushConstants
{
    PrimitivePushConstants push_constants;
};

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

void main()
{
    vec3 world_position = in_position * push_constants.scale + in_instance_position;
    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(world_position, 1.0);
    out_color = in_color;
    out_normal = in_normal;
}
