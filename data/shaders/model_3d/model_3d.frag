#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_overlay_uv;
layout(location = 2) flat in uvec2 in_object_id;
layout(location = 3) flat in float in_overlay_option;
layout(location = 4) in vec2 in_position_xy;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant) uniform constants
{
    uint base_tex;
    uint colormap_tex_idx;
    uint overlay_tex_idx;
    uint overlay_enabled;
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
    bool inside_bbox = in_position_xy.x > PushConstants.bbox_min.x && in_position_xy.x < PushConstants.bbox_max.x &&
            in_position_xy.y > PushConstants.bbox_min.y && in_position_xy.y < PushConstants.bbox_max.y;
    if (inside_bbox)
    {
        discard;
    }

    vec4 base_color = texture(texture_sampler[nonuniformEXT(PushConstants.base_tex)], in_uv);
    vec4 surface_color = vec4(mix(vec3(1, 0, 0), base_color.xyz, base_color.w), 1);
    if (PushConstants.overlay_enabled != 0u)
    {
        vec2 overlay_uv = in_overlay_uv * vec2(PushConstants.overlay_scale_x, PushConstants.overlay_scale_y) +
                vec2(PushConstants.overlay_translation_x, PushConstants.overlay_translation_y);
        vec4 overlay_color = texture(texture_sampler[nonuniformEXT(PushConstants.overlay_tex_idx)], overlay_uv);
        surface_color = mix(base_color, overlay_color, overlay_color.a);
    }

    vec4 road_color = texture(texture_sampler[nonuniformEXT(PushConstants.colormap_tex_idx)], vec2(in_overlay_option, 1.0));
    out_color = vec4(mix(surface_color.xyz, road_color.xyz, in_overlay_option), 1.0);

    out_object_id = in_object_id;
}
