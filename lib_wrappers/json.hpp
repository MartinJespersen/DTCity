#pragma once

namespace city
{
struct Road;
} // namespace city

namespace wrapper
{
static void
OverpassHighways(Arena* arena, city::Road* roads, String8 json);
};
