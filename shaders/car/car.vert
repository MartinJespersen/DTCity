#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 2) in vec4 row0;
layout(location = 3) in vec4 row1;
layout(location = 4) in vec4 row2;
layout(location = 5) in vec4 row3;

layout(location = 0) out vec2 out_uv;

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

void main() {
    mat4 model = mat4(row0, row1, row2, row3);
    gl_Position = camera_ubo.projection * camera_ubo.view * model * vec4(in_position, 1.0);
    out_uv = in_uv;
}
