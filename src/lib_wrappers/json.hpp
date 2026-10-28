#pragma once

#include "base/base_strings.hpp"

namespace osm
{
struct WgsNodeList;
struct Way;
struct NodeParseResult;
struct WayParseResult;
} // namespace osm

namespace wrapper
{
osm::NodeParseResult
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
osm::WayParseResult
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
