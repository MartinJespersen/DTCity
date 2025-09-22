#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uvec2 in_object_id;

layout(location = 0) out vec2 out_uv;
layout(location = 1) flat out uvec2 out_object_id;

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

void main() {
    gl_Position = camera_ubo.projection * camera_ubo.view * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_object_id = in_object_id;
}
