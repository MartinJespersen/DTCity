#version 450

layout(location = 0) in vec2 in_position;

layout(location = 0) out vec2 out_uv;

layout(push_constant) uniform constants
{
    float road_height;
    float texture_scale;
} PushConstants;

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

void main() {
    gl_Position = camera_ubo.projection * camera_ubo.view * vec4(in_position.x, PushConstants.road_height, in_position.y, 1.0);
    out_uv = in_position.xy;
}
