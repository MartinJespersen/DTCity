#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "includes.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_overlay_option;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_overlay_uv;
layout(location = 4) in uvec2 in_object_id;
layout(location = 5) in uint in_road_segment_index;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;
layout(location = 2) flat out uvec2 out_object_id;
layout(location = 3) flat out float out_overlay_option;
layout(location = 4) flat out uint out_road_segment_index;

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

layout(push_constant) uniform Constants
{
    RenderPushConstants push_constants;
};

void main() {
    vec3 pos = in_position;
    pos.z -= push_constants.height_offset;
    gl_Position = vec4(pos, 1.0);
    out_uv = in_uv;
    out_overlay_uv = in_overlay_uv;
    out_object_id = in_object_id;
    out_overlay_option = in_overlay_option;
    out_road_segment_index = in_road_segment_index;
}
