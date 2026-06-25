#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

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

layout(push_constant) uniform constants
{
    uint base_tex;
    uint colormap_tex_idx;
    uint overlay_tex_idx;
    uint overlay_enabled;
    uint64_t colormap_address;
    uint colormap_len;
    float overlay_translation_x;
    float overlay_translation_y;
    float overlay_scale_x;
    float overlay_scale_y;
    vec2 bbox_min;
    vec2 bbox_max;
    float height_offset;
} PushConstants;

void main() {
    vec3 pos = in_position;
    pos.z -= PushConstants.height_offset;
    gl_Position = camera_ubo.projection * camera_ubo.view * vec4(pos, 1.0);
    out_uv = in_uv;
    out_overlay_uv = in_overlay_uv;
    out_object_id = in_object_id;
    out_overlay_option = in_overlay_option;
    out_position_xy = in_position.xy;
}
