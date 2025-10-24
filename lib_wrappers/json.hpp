#pragma once

// ~mgj: forward declaration
namespace city
{
struct Road;
struct RoadNodeList;
struct Way;
} // namespace city

namespace wrapper
{
static Buffer<city::RoadNodeList>
node_buffer_from_simd_json(Arena* arena, String8 json, U64 node_hashmap_size);
static Buffer<city::Way>
way_buffer_from_simd_json(Arena* arena, String8 json);
}; // namespace wrapper
