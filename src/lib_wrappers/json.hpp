#pragma once

#include "base/base_strings.hpp"

namespace osm
{
struct RoadNodeList;
struct Way;
struct RoadNodeParseResult;
struct WayParseResult;
} // namespace osm

namespace wrapper
{
osm::RoadNodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
osm::WayParseResult
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
