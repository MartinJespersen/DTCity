#pragma once

// namespace city
struct osm_RoadNodeList;
struct osm_Way;
struct osm_RoadNodeParseResult;
struct osm_WayParseResult;

namespace wrapper
{
static osm_RoadNodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
static osm_WayParseResult
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
