#pragma once

// ~mgj: forward declaration
namespace city
{
struct Road;
} // namespace city
struct osm_RoadNodeList;
struct osm_Way;

namespace wrapper
{
static Buffer<osm_RoadNodeList>
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
static Buffer<osm_Way>
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
