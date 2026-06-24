#pragma once

namespace osm
{
struct RoadNodeList;
struct Way;
struct RoadNodeParseResult;
struct WayParseResult;
} // namespace osm

namespace wrapper
{
static osm::RoadNodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
static osm::WayParseResult
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
