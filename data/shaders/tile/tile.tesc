#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require
#include "includes.glsl"

layout (vertices = 3) out;

layout(location = 0) in vec2 in_uv[];
layout(location = 1) in vec2 in_overlay_uv[];
layout(location = 2) flat in uvec2 in_object_id[];
layout(location = 3) flat in float in_overlay_option[];
layout(location = 4) flat in uint in_road_segment_index[];


layout(location = 0) out vec2 out_uv[3];
layout(location = 1) out vec2 out_overlay_uv[3];
layout(location = 2) flat out uvec2 out_object_id[3];
layout(location = 3) patch out float out_overlay_option;
layout(location = 4) patch out uint out_road_segment_index;

layout (push_constant) uniform Constants {
    RenderPushConstants push_constants;
};

layout(set = 0, binding = 0) uniform CameraUBO{
    Camera ubo_camera;
};

float screen_space_tess_factor(vec4 p0, vec4 p1)
{
	// Calculate edge mid point
	vec4 midPoint = 0.5 * (p0 + p1);
	// Sphere radius as distance between the control points
	float radius = distance(p0, p1) / 2.0;

	// View space
	vec4 v0 = ubo_camera.view  * midPoint;

	// Project into clip space
	vec4 clip0 = (ubo_camera.projection * (v0 - vec4(radius, vec3(0.0))));
	vec4 clip1 = (ubo_camera.projection * (v0 + vec4(radius, vec3(0.0))));

	// Get normalized device coordinates
	clip0 /= clip0.w;
	clip1 /= clip1.w;

	// Convert to viewport coordinates
	clip0.xy *= ubo_camera.viewport_dim;
	clip1.xy *= ubo_camera.viewport_dim;

	// Return the tessellation factor based on the screen size
	// given by the distance of the two edge control points in screen space
	// and a reference (min.) tessellation size for the edge set by the application
	return clamp(distance(clip0, clip1) / push_constants.tessellated_edge_size * push_constants.tessellation_factor, 1.0, 64.0);
}

// // Checks the current's patch visibility against the frustum using a sphere check
// // Sphere radius is given by the patch size
bool frustum_check()
{
	vec3 p0 = gl_in[0].gl_Position.xyz;
	vec3 p1 = gl_in[1].gl_Position.xyz;
	vec3 p2 = gl_in[2].gl_Position.xyz;
	vec3 center = (p0 + p1 + p2) / 3.0;

	float radius = distance(center, p0);
	radius = max(radius, distance(center, p1));
	radius = max(radius, distance(center, p2));

	vec4 center_pos = vec4(center, 1.0);
	for (int plane_idx = 0; plane_idx < 6; plane_idx++)
	{
		float plane_distance = dot(center_pos, ubo_camera.frustum_planes[plane_idx]);
		if (plane_distance + radius < 0.0)
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
		if (!frustum_check())
		{
			gl_TessLevelInner[0] = 0.0;
			gl_TessLevelOuter[0] = 0.0;
			gl_TessLevelOuter[1] = 0.0;
			gl_TessLevelOuter[2] = 0.0;
		}
		else
		{
			if (push_constants.tessellation_factor > 0.0 && in_road_segment_index[0] != 0)
			{
				gl_TessLevelOuter[0] = screen_space_tess_factor(gl_in[1].gl_Position, gl_in[2].gl_Position);
				gl_TessLevelOuter[1] = screen_space_tess_factor(gl_in[2].gl_Position, gl_in[0].gl_Position);
				gl_TessLevelOuter[2] = screen_space_tess_factor(gl_in[0].gl_Position, gl_in[1].gl_Position);
				gl_TessLevelInner[0] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[1] + gl_TessLevelOuter[2]) / 3.0;
			}
			else
			{
				// Tessellation factor can be set to zero by example
				// to demonstrate a simple passthrough
				gl_TessLevelInner[0] = 1.0;
				gl_TessLevelOuter[0] = 1.0;
				gl_TessLevelOuter[1] = 1.0;
				gl_TessLevelOuter[2] = 1.0;
			}
			}

		out_overlay_option = in_overlay_option[0];
		out_road_segment_index = in_road_segment_index[0];

	}

	gl_out[gl_InvocationID].gl_Position =  gl_in[gl_InvocationID].gl_Position;

	out_uv[gl_InvocationID] = in_uv[gl_InvocationID];
	out_overlay_uv[gl_InvocationID] = in_overlay_uv[gl_InvocationID];
	out_object_id[gl_InvocationID] = in_object_id[gl_InvocationID];
}
