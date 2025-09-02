#pragma once

// ~mgj: forward declaration
namespace city
{
struct Road;
struct NodeWays;
} // namespace city

namespace wrapper
{
static void
OverpassHighwayParse(Arena* arena, String8 json, U64 hashmap_slot_count,
                     city::NodeWays* out_node_ways);
};
