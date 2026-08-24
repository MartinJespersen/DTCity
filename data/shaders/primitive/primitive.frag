#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_direction = normalize(vec3(0.35, -0.45, 0.82));
    float diffuse = max(dot(normalize(in_normal), light_direction), 0.0);
    float lighting = 0.35 + 0.65 * diffuse;
    out_color = vec4(in_color.rgb * lighting, in_color.a);
}
