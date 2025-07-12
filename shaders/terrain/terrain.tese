#version 450

layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

layout(set = 1, binding = 0) uniform UBO_Terrain
{
    float displacement_factor;
    float tessellation_factor;
    float patch_size;
    float tessellated_edge_size;
} terrain_ubo;

layout(set = 1, binding = 1) uniform sampler2D displacement_map;

layout(quads, equal_spacing, ccw) in;

layout(location = 0) in vec2 in_uv[];

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 out_uv;
    vec2 uv1 = mix(in_uv[0], in_uv[1], gl_TessCoord.x);
    vec2 uv2 = mix(in_uv[3], in_uv[2], gl_TessCoord.x);
    out_uv = mix(uv1, uv2, gl_TessCoord.y);

    // Interpolate positions
    vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
    vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
    vec4 pos = mix(pos1, pos2, gl_TessCoord.y);

    // Displace
    vec4 texture_v = textureLod(displacement_map, out_uv, 0.0);
    pos.y += texture_v.r * terrain_ubo.displacement_factor;

    // Perspective projection
    gl_Position = camera_ubo.projection * camera_ubo.view * pos;
    out_color = texture_v; //texture_v;
}
