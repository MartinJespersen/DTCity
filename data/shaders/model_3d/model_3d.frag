#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_overlay_uv;
layout(location = 2) flat in uvec2 in_object_id;
layout(location = 3) flat in float in_overlay_option;
layout(location = 4) in vec2 in_position_xy;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(std430, buffer_reference, buffer_reference_align = 8) readonly buffer Colormap
{
    vec3 data[];
};

layout(push_constant) uniform constants
{
    uint base_tex;
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

void main()
{
    vec4 base_color = texture(texture_sampler[PushConstants.base_tex], in_uv);

    vec4 surface_color;

    if (PushConstants.overlay_enabled != 0u)
    {
        vec2 overlay_uv =
            in_overlay_uv * vec2(PushConstants.overlay_scale_x, PushConstants.overlay_scale_y) +
                vec2(PushConstants.overlay_translation_x, PushConstants.overlay_translation_y);

        vec4 overlay_color =
            texture(texture_sampler[PushConstants.overlay_tex_idx], overlay_uv);

        surface_color = mix(base_color, overlay_color, overlay_color.a);
    }
    else
    {
        surface_color = vec4(mix(vec3(1, 0, 0), base_color.xyz, base_color.w), 1.0);
    }

    vec4 road_color = vec4(0, 0, 0, 1);
    if (PushConstants.colormap_len > 0)
    {
        uint idx = uint(float(PushConstants.colormap_len) * in_overlay_option);
        idx = min(idx, PushConstants.colormap_len - 1);
        Colormap colormap = Colormap(PushConstants.colormap_address);
        road_color = vec4(colormap.data[idx], 1);
    }

    out_color = vec4(mix(surface_color.xyz, road_color.xyz, in_overlay_option), 1.0);
    out_object_id = in_object_id;
}
