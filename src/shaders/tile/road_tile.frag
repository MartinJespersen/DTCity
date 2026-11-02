#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "../shader_shared.h"
#include "tile_surface.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_overlay_uv;
layout(location = 2) flat in uvec2 in_object_id;
layout(location = 3) flat in float in_overlay_option;
layout(location = 4) flat in uint in_agent_count;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(std430, buffer_reference, buffer_reference_align = 8) readonly buffer Colormap
{
    // Uploaded colors are tightly packed RGB float triples (12 bytes per color).
    float data[];
};

void main()
{
    vec4 surface_color = tile_surface_color(in_uv, in_overlay_uv);
    float intensity = in_overlay_option;
    float opacity = in_overlay_option;
    if (push_constants.overlay_option_idx == road_overlay_agent_count)
    {
        float reference_count = max(push_constants.agent_count_reference, 1.0);
        intensity = clamp(log2(1.0 + float(in_agent_count)) / log2(1.0 + reference_count), 0.0, 1.0);
        // Empty ways retain their imagery. Occupied ways share a fixed opacity.
        opacity = in_agent_count > 0u ? 0.8 : 0.0;
    }

    vec3 road_color = vec3(0.0);
    if (push_constants.colormap_len > 0)
    {
        uint idx = uint(float(push_constants.colormap_len) * intensity);
        idx = min(idx, push_constants.colormap_len - 1);
        Colormap colormap = Colormap(push_constants.colormap_address);
        uint component = idx * 3u;
        road_color = vec3(colormap.data[component], colormap.data[component + 1u], colormap.data[component + 2u]);
    }

    out_color = vec4(mix(surface_color.xyz, road_color, opacity), 1.0);
    out_object_id = in_object_id;
}
