#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];

layout(push_constant) uniform constants
{
    uint tex_idx;
} PushConstants;

void main() {
    out_color = texture(texture_sampler[nonuniformEXT(PushConstants.tex_idx)], in_uv);
}
