#pragma once

// ~mgj: forward declaration
namespace city
{
struct Road;
} // namespace city

namespace wrapper
{
static void
OverpassHighwayParse(city::Road* roads, String8 json);
};
