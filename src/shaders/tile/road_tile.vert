#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "../shader_shared.h"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_overlay_uv;
layout(location = 3) in uint in_road_segment_index;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;
layout(location = 2) flat out uvec2 out_object_id;
layout(location = 3) flat out float out_overlay_option;
layout(location = 4) flat out uint out_agent_count;

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

// Counts are shared by every segment with the same way_index. Slot zero is empty.
layout(std430, set = 0, binding = 2) readonly buffer WayAgentCountBuffer
{
    WayAgentCount way_agent_counts[];
};

void main() {
    vec3 pos = in_position;
    pos.z -= push_constants.height_offset;
    out_uv = in_uv;
    out_overlay_uv = in_overlay_uv;

    out_agent_count = 0u;
    out_overlay_option = 0.0;
    if (push_constants.road_test_enabled != 0)
    {
        uint road_segment_index = in_road_segment_index - 1;

        out_object_id = road_segments[road_segment_index].edge_id;
        if (push_constants.overlay_option_idx == road_overlay_agent_count)
        {
            uint way_index = road_segments[road_segment_index].way_index;
            out_agent_count = way_agent_counts[way_index].agent_count;
        }
        else
        {
            out_overlay_option = road_segments[road_segment_index].road_info.options[push_constants.overlay_option_idx];
        }

    }
    else
    {
        out_object_id = uvec2(0, 0);
        out_overlay_option = 0.0;
    }

    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(pos, 1.0);
}
