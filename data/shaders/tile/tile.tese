#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require
#include "includes.glsl"

layout(triangles, equal_spacing, cw) in;

layout(set = 0, binding = 0) uniform CameraUBO
{
    Camera ubo_camera;
};

layout(std430, set = 0, binding = 1) readonly buffer RoadSegmentBuffer
{
    RoadSegment road_segments[];
};

layout(push_constant) uniform PushConstants {
    RenderPushConstants push_constants;
};

layout(location = 0) in vec2 in_uv[];
layout(location = 1) in vec2 in_overlay_uv[];
layout(location = 2) flat in uvec2 in_object_id[];
layout(location = 3) patch in float in_overlay_option;
layout(location = 4) patch in uint in_road_segment_index;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_overlay_uv;
layout(location = 2) flat out uvec2 out_object_id;
layout(location = 3) flat out float out_overlay_option;

void main()
{
    out_uv = in_uv[0] * gl_TessCoord.x + in_uv[1] * gl_TessCoord.y + in_uv[2] * gl_TessCoord.z;
    out_overlay_uv = in_overlay_uv[0] * gl_TessCoord.x + in_overlay_uv[1] * gl_TessCoord.y + in_overlay_uv[2] * gl_TessCoord.z;
    out_object_id = in_object_id[0];
    out_overlay_option = in_overlay_option;
    vec4 pos = gl_TessCoord.x * gl_in[0].gl_Position + gl_TessCoord.y * gl_in[1].gl_Position + gl_TessCoord.z * gl_in[2].gl_Position;

    if (push_constants.road_test_enabled != 0)
    {
        vec2 road_segment[CORNERS_COUNT] = road_segments[in_road_segment_index].positions;
        if (is_face_inside_road_segment(road_segment, pos.xy))
        {
            out_object_id = road_segments[in_road_segment_index].id;
            out_overlay_option = in_overlay_option;
        }
        else
        {
            out_object_id = uvec2(0, 0);
            out_overlay_option = 0.0;
        }
    }

    gl_Position = ubo_camera.projection * ubo_camera.view * pos;
}
