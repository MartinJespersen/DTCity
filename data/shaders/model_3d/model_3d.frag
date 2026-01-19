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
    out_color = texture(texture_sampler[nonuniformEXT(PushConstants.base_tex)], in_uv);
    out_object_id = in_object_id;
}
