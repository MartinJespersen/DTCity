#version 450

layout(location = 0) in vec4 fragColor; // Input texture coordinates
layout(location = 1) in vec2 inCenter;
layout(location = 2) in vec2 inHalfSize;
layout(location = 3) in vec2 inPos;
layout(location = 4) in float softness;
layout(location = 5) in float borderThickness;
layout(location = 6) in float cornerRadius;
layout(location = 7) flat in uint inAttributes;

layout(location = 0) out vec4 outColor;    // Output color

float borderFactor = 1.0;

const uint hot_t = (1u << 0);
const uint active_t = (1u << 1u);

float RoundedRectSDF(
    vec2 sample_pos,
    vec2 rect_center,
    vec2 rect_half_size,
    float r
) {
    vec2 d2 = (abs(rect_center - sample_pos) -
        rect_half_size +
        vec2(r, r));
    return min(max(d2.x, d2.y), 0.0) + length(max(d2, 0.0)) - r;
}
void main() {
    vec2 centerPosPx = inCenter;
    vec2 halfSizePx = inHalfSize;
    vec2 posPx = inPos;
    float transparency = fragColor.a;

    vec2 softnessPadding = vec2(max(0, softness * 2 - 1), max(0, softness * 2 - 1)); // not sure why we need to multiply by 2 and - 1 as sofness is in pixels.
    if(borderThickness != 0.0) {
        vec2 interiorHalfSize = halfSizePx - vec2(borderThickness);
        float interiorRadiusReduceF = min(interiorHalfSize.x / halfSizePx.x, interiorHalfSize.y / halfSizePx.y);
        float interiorCornerRadius = (cornerRadius *
            interiorRadiusReduceF *
            interiorRadiusReduceF);
        float insideD = RoundedRectSDF(posPx, centerPosPx, interiorHalfSize - softnessPadding, interiorCornerRadius);
        float insideF = 1.0 - smoothstep(0.0, 2 * softness, insideD);
        borderFactor = insideF;
    }

    float dist = RoundedRectSDF(posPx, centerPosPx, halfSizePx - softnessPadding, cornerRadius);
    float sdfFactor = 1.0 - smoothstep(0.0, 2 * softness, dist);
    transparency = (inAttributes & hot_t) != 0 ? fragColor.a + ((1 - fragColor.a) * inPos.y) : fragColor.a;
    transparency = (inAttributes & active_t) != 0 ? fragColor.a : transparency;
    outColor = vec4(fragColor.xyz * sdfFactor * borderFactor, transparency);
}
