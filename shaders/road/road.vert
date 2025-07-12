#version 450

layout(location = 0) in vec2 in_position;

layout(push_constant) uniform constants
{
    mat4 model;
    float road_width;
    float road_height;
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
}
