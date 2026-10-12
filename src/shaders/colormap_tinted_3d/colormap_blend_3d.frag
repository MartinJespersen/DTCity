#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uvec2 in_object_id;
layout(location = 2) flat in vec2 in_colormap;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant, std430) uniform Constants
{
    Blend3dPushConstants push_constants;
};

void main() {
    vec4 tex_color = texture(texture_sampler[nonuniformEXT(push_constants.texture_index)], in_uv);
    vec4 colormap = texture(texture_sampler[nonuniformEXT(push_constants.colormap_index)], vec2(in_colormap.x, 1));
    vec3 mixing = mix(tex_color.rgb, colormap.rgb, in_colormap.x);
    out_color = vec4(mixing, tex_color.a);
    out_object_id = in_object_id;
}
