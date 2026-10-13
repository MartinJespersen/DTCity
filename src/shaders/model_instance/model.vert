#version 450
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"

layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec3 vertex_normal;
layout(location = 2) out vec2 vertex_uv;
layout(std140, set = 0, binding = 0) uniform Camera { CameraUniformBuffer camera_ubo; };
layout(std430, set = 0, binding = 1) readonly buffer Vertices { uint words[]; } vertices;
layout(std430, set = 0, binding = 3) readonly buffer Transforms { mat4 models[]; } transforms;
layout(push_constant, std430) uniform PushConstants { MeshInstancePushConstants pc; };

void main()
{
    uint instance = pc.indirect_first_instance != 0 ? uint(gl_InstanceIndex) : pc.first_instance;
    mat4 model = transforms.models[pc.transform_offset + instance];
    uint base = uint(gl_VertexIndex) * pc.vertex_stride;
    vec3 position = uintBitsToFloat(uvec3(vertices.words[base + pc.position_offset],
        vertices.words[base + pc.position_offset + 1], vertices.words[base + pc.position_offset + 2]));
    vec3 normal = uintBitsToFloat(uvec3(vertices.words[base + pc.normal_offset],
        vertices.words[base + pc.normal_offset + 1], vertices.words[base + pc.normal_offset + 2]));
    gl_Position = camera_ubo.proj * camera_ubo.view * model * vec4(position, 1.0);
    vertex_normal = transpose(inverse(mat3(model))) * normal;
    vertex_color = uintBitsToFloat(uvec4(vertices.words[base + pc.color_offset],
        vertices.words[base + pc.color_offset + 1], vertices.words[base + pc.color_offset + 2],
        vertices.words[base + pc.color_offset + 3]));
    vertex_uv = uintBitsToFloat(uvec2(vertices.words[base + pc.uv_offset], vertices.words[base + pc.uv_offset + 1]));
}
