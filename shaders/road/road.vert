#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in uint road_id;

layout(location = 0) out uint road_id_out;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    road_id_out = road_id;
}
