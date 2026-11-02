#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "../shader_shared.h"
#include "tile_surface.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_overlay_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

void main()
{
    out_color = vec4(tile_surface_color(in_uv, in_overlay_uv).xyz, 1.0);
    out_object_id = uvec2(0, 0);
}
