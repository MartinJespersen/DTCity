#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
layout(set = 1, binding = 0) uniform sampler2D texture_sampler[];
layout(push_constant) uniform PushConstants
{
    uint first_meshlet;
    uint first_instance;
    uint transform_offset;
    uint vertex_stride;
    uint position_offset;
    uint normal_offset;
    uint color_offset;
    uint uv_offset;
    uint texture_index;
    uint textured;
    float lod_error_pixels;
} pc;
void main()
{
    if (pc.textured != 0)
    {
        // Preserve the existing agent material/transparent-placeholder behavior.
        vec4 sampled = texture(texture_sampler[nonuniformEXT(pc.texture_index)], in_uv);
        out_color = sampled.a != 0.0 ? sampled : in_color;
    }
    else
    {
        vec3 normal = normalize(in_normal);
        vec3 light_direction = normalize(vec3(0.35, -0.45, 0.82));
        float lighting = 0.35 + 0.65 * max(dot(normal, light_direction), 0.0);
        out_color = vec4(in_color.rgb * lighting, in_color.a);
    }
}
