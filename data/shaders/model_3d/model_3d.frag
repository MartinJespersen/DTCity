#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uvec2 in_object_id;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant) uniform constants
{
    uint base_tex;
} PushConstants;

void main() {
    vec4 sampler_color = texture(texture_sampler[nonuniformEXT(PushConstants.base_tex)], in_uv);
    vec3 road_color = vec3(1, 0, 0);
    out_color = vec4(mix(sampler_color.xyz, road_color, in_object_id.x), 1);

    out_object_id = in_object_id;
}
