#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uvec2 in_object_id;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec2 out_object_id;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler;

void main() {
    vec4 tex_color = texture(texture_sampler, in_uv);
    vec3 blended = mix(tex_color.rgb, in_color.rgb, in_color.a);
    out_color = vec4(blended, tex_color.a);
    out_object_id = in_object_id;
}
