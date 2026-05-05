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

layout(set = 1, binding = 0) uniform UBO
{
    float tessellation_factor;
    float tessellated_edge_size;
    float patch_size;
} patch_ubo;

layout(vertices = 4) out;

// layout (location = 0) in vec3 inNormal[];
layout(location = 0) in vec2 in_uv[];

// layout (location = 0) out vec3 outNormal[4];
layout(location = 0) out vec2 out_uv[4];

// Calculate the tessellation factor based on screen space
// dimensions of the edge
float screenSpaceTessFactor(vec4 p0, vec4 p1)
{
    // Calculate edge mid point
    vec4 midPoint = 0.5 * (p0 + p1);
    // Sphere radius as distance between the control points
    float radius = distance(p0, p1) / 2.0;

    // View space
    vec4 v0 = camera_ubo.view * midPoint;

    // Project into clip space
    vec4 clip0 = (camera_ubo.projection * (v0 - vec4(radius, vec3(0.0))));
    vec4 clip1 = (camera_ubo.projection * (v0 + vec4(radius, vec3(0.0))));

    // Get normalized device coordinates
    clip0 /= clip0.w;
    clip1 /= clip1.w;

    // Convert to viewport coordinates
    clip0.xy *= camera_ubo.viewport_dim;
    clip1.xy *= camera_ubo.viewport_dim;

    // Return the tessellation factor based on the screen size
    // given by the distance of the two edge control points in screen space
    // and a reference (min.) tessellation size for the edge set by the application
    return clamp(distance(clip0, clip1) / patch_ubo.tessellated_edge_size * patch_ubo.tessellation_factor, 1.0, 64.0);
}

// Checks the current's patch visibility against the frustum using a sphere check
// Sphere radius is given by the patch size
bool frustumCheck()
{
    // Fixed radius (increase if patch size is increased in example)
    const float radius = patch_ubo.patch_size / 2.0;
    vec4 pos = gl_in[gl_InvocationID].gl_Position;

    // Check sphere against frustum planes
    for (int i = 0; i < 6; i++) {
        if (dot(pos, camera_ubo.frustum_planes[i]) + radius < 0.0)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    if (gl_InvocationID == 0)
    {
        if (!frustumCheck())
        {
            gl_TessLevelInner[0] = 0.0;
            gl_TessLevelInner[1] = 0.0;
            gl_TessLevelOuter[0] = 0.0;
            gl_TessLevelOuter[1] = 0.0;
            gl_TessLevelOuter[2] = 0.0;
            gl_TessLevelOuter[3] = 0.0;
        }
        else
        {
            if (patch_ubo.tessellation_factor > 0.0)
            {
                gl_TessLevelOuter[0] = screenSpaceTessFactor(gl_in[3].gl_Position, gl_in[0].gl_Position);
                gl_TessLevelOuter[1] = screenSpaceTessFactor(gl_in[0].gl_Position, gl_in[1].gl_Position);
                gl_TessLevelOuter[2] = screenSpaceTessFactor(gl_in[1].gl_Position, gl_in[2].gl_Position);
                gl_TessLevelOuter[3] = screenSpaceTessFactor(gl_in[2].gl_Position, gl_in[3].gl_Position);
                gl_TessLevelInner[0] = mix(gl_TessLevelOuter[0], gl_TessLevelOuter[3], 0.5);
                gl_TessLevelInner[1] = mix(gl_TessLevelOuter[2], gl_TessLevelOuter[1], 0.5);
            }
            else
            {
                // Tessellation factor can be set to zero by example
                // to demonstrate a simple passthrough
                gl_TessLevelInner[0] = 4.0;
                gl_TessLevelInner[1] = 4.0;
                gl_TessLevelOuter[0] = 4.0;
                gl_TessLevelOuter[1] = 4.0;
                gl_TessLevelOuter[2] = 4.0;
                gl_TessLevelOuter[3] = 4.0;
            }
        }
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    out_uv[gl_InvocationID] = in_uv[gl_InvocationID];
}
