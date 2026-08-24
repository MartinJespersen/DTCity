#version 450

layout(location = 0) in vec3 in_instance_position;
layout(location = 1) in vec3 in_position;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec3 out_normal;

layout(push_constant) uniform PushConstants
{
    float scale;
} push_constants;

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

void main()
{
    vec3 world_position = in_position * push_constants.scale + in_instance_position;
    gl_Position = camera_ubo.projection * camera_ubo.view * vec4(world_position, 1.0);
    out_color = in_color;
    out_normal = in_normal;
}
