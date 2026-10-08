#pragma once

#include <glm/glm.hpp>
#include "base/base_math.h"

namespace util
{
Rng2F64
wgs84_bbox_from_btm_right_corner(F64 lon, F64 lat, F64 width, F64 height);

S32
target_srid_from_wgs84(Vec2F64 wgs84_point);

glm::dvec3
ecef_from_wgs84(F64 lon, F64 lat);

} // namespace util
