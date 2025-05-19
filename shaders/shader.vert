#version 450

layout(location = 0) in vec2 pos0;
layout(location = 1) in vec2 pos1;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inSoftness;
layout(location = 4) in float inBorderThickness;
layout(location = 5) in float inCornerRadius;
layout(location = 6) in uint inAttributes;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 outCenter;
layout(location = 2) out vec2 outHalfSize;
layout(location = 3) out vec2 outPos;
layout(location = 4) out float outSoftness;
layout(location = 5) out float outBorderThickness;
layout(location = 6) out float outCornerRadius;
layout(location = 7) out uint outAttributes;

const vec2[4] vertices = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
const vec2[4] uvs = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

layout(push_constant) uniform PushConstants {
    vec2 resolution;
} pushConstants;

void main() {
    vec2 halfSize = (pos1 - pos0) / 2;
    vec2 center = (pos0 + pos1) / 2;
    vec2 position = center + (vertices[gl_VertexIndex] * halfSize);
    gl_Position = vec4(2 * (position.x) / pushConstants.resolution.x - 1, 2 * (position.y / pushConstants.resolution.y) - 1, 0.0, 1.0);

    outPos = position;
    outCenter = center;
    outHalfSize = halfSize;
    fragColor = inColor;
    outSoftness = inSoftness;
    outBorderThickness = inBorderThickness;
    outCornerRadius = inCornerRadius;
    outAttributes = inAttributes;
}
