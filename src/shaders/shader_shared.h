#ifndef DTCITY_SHADER_SHARED_H
#define DTCITY_SHADER_SHARED_H

// Packed model geometry layout shared by the C++ writer and GLSL readers.
// All offsets and strides are in uint32 words, except triangle bit shifts.
#define MODEL_GEOMETRY_CENTER_X_OFFSET 0
#define MODEL_GEOMETRY_CENTER_Y_OFFSET 1
#define MODEL_GEOMETRY_CENTER_Z_OFFSET 2
#define MODEL_GEOMETRY_CENTER_COMPONENT_COUNT 3
#define MODEL_GEOMETRY_RADIUS_OFFSET 3
#define MODEL_GEOMETRY_LOD_COUNT_OFFSET 4
// Header words 5..7 are reserved; the LOD table starts at word 8.
#define MODEL_GEOMETRY_LOD_TABLE_OFFSET 8
#define MODEL_LOD_MAX_COUNT 4
#define MODEL_LOD_ENTRY_WORD_COUNT 4
// Data points to meshlet descriptors on the mesh path, or indices on the fallback.
// Element count is correspondingly a meshlet count or an index count.
#define MODEL_LOD_DATA_OFFSET 0
#define MODEL_LOD_ELEMENT_COUNT_OFFSET 1
#define MODEL_LOD_ERROR_OFFSET 2
#define MODEL_LOD_RESERVED_OFFSET 3
#define MODEL_GEOMETRY_HEADER_WORD_COUNT (MODEL_GEOMETRY_LOD_TABLE_OFFSET + MODEL_LOD_MAX_COUNT * MODEL_LOD_ENTRY_WORD_COUNT)

#define MODEL_MESHLET_DESCRIPTOR_WORD_COUNT 4
#define MODEL_MESHLET_VERTEX_OFFSET 0
#define MODEL_MESHLET_TRIANGLE_OFFSET 1
#define MODEL_MESHLET_VERTEX_COUNT_OFFSET 2
#define MODEL_MESHLET_TRIANGLE_COUNT_OFFSET 3
// Each triangle stores three local vertex indices in one word.
#define MODEL_TRIANGLE_INDEX_MASK 255u
#define MODEL_TRIANGLE_INDEX_0_SHIFT 0
#define MODEL_TRIANGLE_INDEX_1_SHIFT 8
#define MODEL_TRIANGLE_INDEX_2_SHIFT 16

// VkDrawIndexedIndirectCommand layout, also measured in uint32 words.
#define MODEL_DRAW_INDEXED_WORD_COUNT 5
#define MODEL_DRAW_INDEX_COUNT_OFFSET 0
#define MODEL_DRAW_INSTANCE_COUNT_OFFSET 1
#define MODEL_DRAW_FIRST_INDEX_OFFSET 2
#define MODEL_DRAW_VERTEX_OFFSET 3
#define MODEL_DRAW_FIRST_INSTANCE_OFFSET 4

// GPU data only. Bindings and shader-stage interfaces belong in the shader files.
// Root records (buffer elements, push constants, and task payloads) start on
// 16-byte boundaries. Nested structs and members follow std430 alignment.
// The camera layout also matches std140, so it needs no optional UBO feature.
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include "base/base_math.h"

namespace shader
{
using uint = std::uint32_t;
using uint64_t = std::uint64_t;
// Explicit qualifiers keep layouts independent of GLM's default alignment flags.
using vec2 = glm::vec<2, float, glm::packed_highp>;
using vec4 = glm::vec<4, float, glm::packed_highp>;
using mat4 = glm::mat<4, 4, float, glm::packed_highp>;
#define SHADER_ALIGN(bytes) alignas(bytes)
#define SHADER_ROOT alignas(16)
#define SHADER_ROAD_POINT Vec2F32
#define SHADER_EDGE_ID std::uint64_t
#else
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#define SHADER_ALIGN(bytes)
#define SHADER_ROOT
#define SHADER_ROAD_POINT vec2
// Road picking writes the low and high halves into an R32G32_UINT attachment.
#define SHADER_EDGE_ID uvec2
#endif

const uint road_corner_count = 4;
const uint road_option_count = 5;
const uint frustum_plane_count = 6;

// Shared by the tile hierarchy and per-mesh triangle trees. Node zero is the root.
// Internal nodes have count == 0; first identifies two adjacent children.
// Leaves reference contiguous mesh entries or triangles (three U32 indices each).
struct SHADER_ROOT TileBvhGpuNode
{
    float bounds_min[3];
    uint count;
    float bounds_max[3];
    uint first;
};

// Buffer indices address the mesh-address table owned by the complete BVH resource.
// Three affine rows transform city-local points/directions into mesh space.
struct SHADER_ROOT TileBvhGpuEntry
{
    float city_to_mesh_rows[3][4];
    uint mesh_buffer_index;
    uint triangle_bvh_root;
    uint vertex_position_offset_bytes;
    uint vertex_stride_bytes;
    uint index_offset_bytes;
    // Stable city registry identity, preserved when the outer BVH reorders entries.
    uint entry_handle_idx;
    uint entry_handle_gen_id;
    uint reserved;
};

// Addresses remain valid while the immutable BVH resource is retained.
struct SHADER_ROOT TileBvhGpuMeshAddresses
{
    uint64_t vertices;
    uint64_t indices;
    uint64_t nodes;
    uint64_t reserved;
};

struct SHADER_ROOT TileHeightGpuQuery
{
    float origin[3];
    uint reserved_origin;
    float direction[3];
    uint reserved_direction;
};

struct SHADER_ROOT TileHeightGpuResult
{
    float hit_distance;
    uint hit;
    // Zero on a miss. Includes the generation so unloaded/reused slots are distinct.
    uint entry_handle_idx;
    uint entry_handle_gen_id;
};

struct SHADER_ROOT TileHeightPushConstants
{
    uint64_t nodes;
    uint64_t entries;
    uint64_t meshes;
    uint64_t queries;
    uint64_t results;
    uint node_count;
    uint query_count;
    uint reserved[4];
};

struct SHADER_ALIGN(16) Frustum
{
    SHADER_ALIGN(16)
    vec4 planes[frustum_plane_count];
};

struct SHADER_ROOT CameraUniformBuffer
{
    SHADER_ALIGN(16)
    mat4 view;
    SHADER_ALIGN(16)
    mat4 proj;
    Frustum frustum;
    SHADER_ALIGN(8)
    vec2 viewport_dim;
    SHADER_ALIGN(8)
    vec2 _padding;
};

struct SHADER_ROOT TilePipelinePushConstants
{
    uint tex_idx;
    uint overlay_tex_idx;
    uint overlay_enabled;
    uint _padding_address; // The following 64-bit member needs 8-byte alignment.
    uint64_t colormap_address;
    uint colormap_len;
    float overlay_translation_x;
    float overlay_translation_y;
    float overlay_scale_x;
    float overlay_scale_y;
    float height_offset;
    float lod_fade;
    uint road_test_enabled;
    uint overlay_option_idx;
    uint _padding_tail;
};

// Explicit tail padding also gives GLSL arrays the same stride as C++ arrays.
struct SHADER_ROOT Blend3dPushConstants
{
    uint texture_index;
    uint colormap_index;
    uint _padding[2];
};

struct SHADER_ROOT RoadIntersectionPushConstants
{
    uint road_segment_buffer_elem_count;
    uint overlay_option_idx;
    uint _padding[2];
};

struct SHADER_ROOT PrimitivePushConstants
{
    float scale;
    uint _padding[3];
};

struct SHADER_ROOT MeshInstancePushConstants
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
    uint instance_count;
    uint draw_offset;
    uint indirect_first_instance;
    uint _padding[2];
};

struct SHADER_ROOT LodPayload
{
    uint descriptor;
    uint instance_index;
    uint _padding[2];
};

struct SHADER_ALIGN(8) Bounds
{
    SHADER_ALIGN(8)
    vec2 min;
    SHADER_ALIGN(8)
    vec2 max;
};

struct RoadInfo
{
    float options[road_option_count];
};

struct SHADER_ROOT RoadSegmentCorners
{
    SHADER_EDGE_ID edge_id;
    SHADER_ALIGN(8)
    SHADER_ROAD_POINT corners[road_corner_count];
    RoadInfo road_info;
    uint _padding;
};

#ifdef __cplusplus
// Fail compilation if a host layout no longer matches the shader ABI.
static_assert(sizeof(TileBvhGpuNode) == 32);
static_assert(sizeof(TileBvhGpuMeshAddresses) == 32);
static_assert(sizeof(TileHeightGpuQuery) == 32);
static_assert(sizeof(TileHeightGpuResult) == 16);
static_assert(sizeof(TileHeightPushConstants) == 64);
static_assert(offsetof(TileBvhGpuNode, count) == 12);
static_assert(offsetof(TileBvhGpuNode, bounds_max) == 16);
static_assert(offsetof(TileBvhGpuNode, first) == 28);
static_assert(sizeof(TileBvhGpuEntry) == 80);
static_assert(offsetof(TileBvhGpuEntry, mesh_buffer_index) == 48);
static_assert(offsetof(TileBvhGpuEntry, vertex_position_offset_bytes) == 56);
static_assert(offsetof(TileBvhGpuEntry, entry_handle_idx) == 68);
static_assert(sizeof(Frustum) == 96);
static_assert(sizeof(CameraUniformBuffer) == 240);
static_assert(offsetof(CameraUniformBuffer, proj) == 64);
static_assert(offsetof(CameraUniformBuffer, frustum) == 128);
static_assert(offsetof(CameraUniformBuffer, viewport_dim) == 224);
static_assert(sizeof(TilePipelinePushConstants) == 64);
static_assert(offsetof(TilePipelinePushConstants, colormap_address) == 16);
static_assert(offsetof(TilePipelinePushConstants, colormap_len) == 24);
static_assert(offsetof(TilePipelinePushConstants, overlay_option_idx) == 56);
static_assert(sizeof(Blend3dPushConstants) == 16);
static_assert(sizeof(RoadIntersectionPushConstants) == 16);
static_assert(sizeof(PrimitivePushConstants) == 16);
static_assert(sizeof(MeshInstancePushConstants) == 64);
static_assert(offsetof(MeshInstancePushConstants, lod_error_pixels) == 40);
static_assert(sizeof(LodPayload) == 16);
static_assert(sizeof(Bounds) == 16);
static_assert(alignof(Bounds) == 8);
static_assert(alignof(Frustum) == 16);
static_assert(alignof(RoadInfo) == 4);
static_assert(sizeof(RoadInfo) == 20);
static_assert(sizeof(RoadSegmentCorners) == 64);
static_assert(offsetof(RoadSegmentCorners, corners) == 8);
static_assert(offsetof(RoadSegmentCorners, road_info) == 40);
} // namespace shader
#endif

#undef SHADER_ALIGN
#undef SHADER_ROOT
#undef SHADER_ROAD_POINT
#undef SHADER_EDGE_ID
#endif // DTCITY_SHADER_SHARED_H
