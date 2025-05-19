#version 450

layout(location = 0) in vec2 fragCoord; // Input texture coordinates
layout(location = 0) out vec4 outColor;    // Output color
layout(binding = 0) uniform sampler2D tex; // Input texture

vec3 color = vec3(1.0, 0.0, 0.0);
void main() {

    vec4 sampled = vec4(1.0, 1.0, 1.0, texelFetch(tex, ivec2(fragCoord), 0).r);
    outColor = vec4(color, 1.0) * sampled;
}