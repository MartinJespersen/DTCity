#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "../shader_shared.h"
layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec3 vertex_normal;
layout(location = 2) in vec2 vertex_uv;
layout(location = 0) out vec4 out_color;
layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];
layout(push_constant, std430) uniform PushConstants
{
    MeshInstancePushConstants pc;
};
void main()
{
    if (pc.textured != 0)
    {
        // Preserve the existing agent material/transparent-placeholder behavior.
        vec4 sampled = texture(texture_sampler[nonuniformEXT(pc.texture_index)], vertex_uv);
        out_color = sampled.a != 0.0 ? sampled : vertex_color;
    }
    else
    {
        vec3 normal = normalize(vertex_normal);
        vec3 light_direction = normalize(vec3(0.35, -0.45, 0.82));
        float lighting = 0.35 + 0.65 * max(dot(normal, light_direction), 0.0);
        out_color = vec4(vertex_color.rgb * lighting, vertex_color.a);
    }
}
