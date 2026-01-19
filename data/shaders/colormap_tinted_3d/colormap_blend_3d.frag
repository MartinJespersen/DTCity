#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uvec2 in_object_id;
layout(location = 2) flat in vec2 in_colormap;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant) uniform constants
{
    uint base_tex;
    uint colormap_tex;
} PushConstants;

void main() {
    vec4 tex_color = texture(texture_sampler[nonuniformEXT(PushConstants.base_tex)], in_uv);
    vec4 colormap = texture(texture_sampler[nonuniformEXT(PushConstants.colormap_tex)], vec2(in_colormap.x, 1));
    vec3 mixing = mix(tex_color.rgb, colormap.rgb, in_colormap.x);
    out_color = vec4(mixing, tex_color.a);
    out_object_id = in_object_id;
}
