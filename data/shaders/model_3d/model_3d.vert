#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_overlay_option;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_overlay_uv;
layout(location = 4) in uvec2 in_object_id;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;
layout(location = 2) flat out uvec2 out_object_id;
layout(location = 3) flat out float out_overlay_option;
layout(location = 4) out vec2 out_position_xy;

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
    out_overlay_uv = in_overlay_uv;
    out_object_id = in_object_id;
    out_overlay_option = in_overlay_option;
    out_position_xy = in_position.xy;
}
