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
layout(set = 0, binding = 0) uniform UBO_Camera
{
    mat4 view;
    mat4 projection;
    vec4 frustum_planes[6];
    vec2 viewport_dim;
} camera_ubo;

layout(set = 1, binding = 0) uniform UBO_Patch
{
    float tessellation_factor;
    float tessellated_edge_size;
    float patch_size;
} patch_ubo;

layout(quads, equal_spacing, ccw) in;

layout(location = 0) in vec2 in_uv[];

layout(location = 0) out vec2 out_uv;

void main()
{
    // Interpolate UV coordinates
    vec2 uv1 = mix(in_uv[0], in_uv[1], gl_TessCoord.x);
    vec2 uv2 = mix(in_uv[3], in_uv[2], gl_TessCoord.x);
    out_uv = mix(uv1, uv2, gl_TessCoord.y);

    // Interpolate positions
    vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
    vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
    vec4 pos = mix(pos1, pos2, gl_TessCoord.y);

    // Perspective projection
    gl_Position = camera_ubo.projection * camera_ubo.view * pos;
}
