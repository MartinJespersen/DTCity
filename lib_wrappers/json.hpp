#pragma once

// ~mgj: forward declaration
namespace city
{
struct Road;
struct NodeWays;
} // namespace city

namespace wrapper
{
static city::NodeWays
OverpassNodeWayParse(Arena* arena, String8 json, U64 node_hashmap_size);
};
