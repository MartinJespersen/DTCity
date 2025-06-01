#version 450

layout(binding = 0) uniform TerrainTransform {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // gl_Position = vec4(inPosition, 1.0);
    fragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red color for the terrain
}