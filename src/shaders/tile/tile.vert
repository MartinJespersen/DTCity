#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "includes.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_overlay_uv;
layout(location = 3) in uint in_road_segment_index;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;
layout(location = 2) flat out uvec2 out_object_id;
layout(location = 3) flat out float out_overlay_option;

layout(std140, set = 0, binding = 0) uniform UBO_Camera
{
    CameraUniformBuffer camera_ubo;
};

layout(push_constant, std430) uniform Constants
{
    TilePipelinePushConstants push_constants;
};

layout(std430, set = 0, binding = 1) readonly buffer RoadSegmentBuffer
{
    RoadSegmentCorners road_segments[];
};

void main() {
    vec3 pos = in_position;
    pos.z -= push_constants.height_offset;
    gl_Position = vec4(pos, 1.0);
    out_uv = in_uv;
    out_overlay_uv = in_overlay_uv;

    if (push_constants.road_test_enabled != 0 && in_road_segment_index != 0)
    {
        uint road_segment_index = in_road_segment_index - 1;

        out_object_id = road_segments[road_segment_index].edge_id;
        out_overlay_option = road_segments[road_segment_index].road_info.options[push_constants.overlay_option_idx];

    }
    else
    {
        out_object_id = uvec2(0, 0);
        out_overlay_option = 0.0;
    }

    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(pos, 1.0);
}
