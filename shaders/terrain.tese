#version 450
/* Copyright (c) 2019-2024, Sascha Willems
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

layout(set = 0, binding = 0) uniform UBO
{
    mat4 projection;
    mat4 view;
    mat4 model;
    // vec4 lightPos;
    vec4 frustum_planes[6];
    float displacement_factor;
    float tessellation_factor;
    float patch_size;
    vec2 viewport_dim;
    float tessellated_edge_size;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D displacement_map;

layout(quads, equal_spacing, cw) in;

// layout (location = 0) in vec3 inNormal[];
layout(location = 0) in vec2 in_uv[];

layout(location = 0) out vec4 out_color;
// layout (location = 0) out vec3 out_normal;
// layout (location = 1) out vec2 out_uv;
// layout (location = 2) out vec3 out_view_vec;
// layout (location = 3) out vec3 outLightVec;
// layout (location = 3) out vec3 out_eye_pos;
// layout (location = 4) out vec3 out_world_pos;

void main()
{
    // Interpolate UV coordinates
    vec2 out_uv;
    vec2 uv1 = mix(in_uv[0], in_uv[1], gl_TessCoord.x);
    vec2 uv2 = mix(in_uv[3], in_uv[2], gl_TessCoord.x);
    out_uv = mix(uv1, uv2, gl_TessCoord.y);

    // vec3 n1 = mix(inNormal[0], inNormal[1], gl_TessCoord.x);
    // vec3 n2 = mix(inNormal[3], inNormal[2], gl_TessCoord.x);
    // out_normal = mix(n1, n2, gl_TessCoord.y);

    // Interpolate positions
    vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
    vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
    vec4 pos = mix(pos1, pos2, gl_TessCoord.y);
    // Displace
    vec4 texture_v = textureLod(displacement_map, out_uv, 0.0);
    pos.y -= texture_v.r * ubo.displacement_factor;
    // Perspective projection
    gl_Position = ubo.projection * ubo.view * ubo.model * pos;
    out_color = texture_v;

    // Calculate vectors for lighting based on tessellated position
    // out_view_vec = -pos.xyz;
    // outLightVec = normalize(ubo.lightPos.xyz + out_view_vec);
    // out_world_pos = pos.xyz;
    // out_eye_pos = vec3(ubo.modelview * pos);
}
