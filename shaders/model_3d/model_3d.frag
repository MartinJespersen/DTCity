#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uvec2 in_object_id;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler;

void main() {
    out_color = texture(texture_sampler, in_uv);
    out_object_id = in_object_id;
}
