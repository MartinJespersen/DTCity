// Both tile pipelines use the same imagery and LOD fade at their shared edges.
layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant, std430) uniform Constants
{
    TilePipelinePushConstants push_constants;
};

vec4 tile_surface_color(vec2 uv, vec2 overlay_uv)
{
    vec4 base_color = texture(texture_sampler[push_constants.tex_idx], uv);
    if (push_constants.overlay_enabled != 0u)
    {
        vec2 raster_uv = overlay_uv * vec2(push_constants.overlay_scale_x, push_constants.overlay_scale_y) +
            vec2(push_constants.overlay_translation_x, push_constants.overlay_translation_y);
        vec4 overlay_color = texture(texture_sampler[push_constants.overlay_tex_idx], raster_uv);
        return mix(base_color, overlay_color, overlay_color.a);
    }
    return vec4(base_color.xyz * base_color.w, 1.0);
}
