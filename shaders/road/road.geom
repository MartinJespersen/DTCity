#version 450

layout (lines_adjacency) in;
layout(triangle_strip, max_vertices = 8) out;

layout( push_constant ) uniform constants
{
	float road_width;
	float road_height;
} PushConstants;

void main() {
    vec2 node_0 = gl_in[1].gl_Position.xy;
    vec2 node_1 = gl_in[2].gl_Position.xy;
    vec2 next_node = gl_in[3].gl_Position.xy;
    float road_half_width = PushConstants.road_width / 2.0;

    vec2 vec_0 = node_1 - node_0;
    vec2 orthogonal_vec = vec2(-vec_0.y, vec_0.x);
    vec2 normal_vec = normalize(orthogonal_vec);
    vec2 normal_scaled = normal_vec * road_half_width;
    vec2 normal_scaled_reversed = -normal_scaled;

    gl_Position = vec4(node_0 + normal_scaled, PushConstants.road_height, 1.0);
    EmitVertex();
    gl_Position = vec4(node_0 + normal_scaled_reversed, PushConstants.road_height, 1.0);
    EmitVertex();
    gl_Position = vec4(node_1 + normal_scaled, PushConstants.road_height, 1.0);
    EmitVertex();
    gl_Position = vec4(node_1 + normal_scaled_reversed, PushConstants.road_height, 1.0);
    EmitVertex();

    EndPrimitive();
}
