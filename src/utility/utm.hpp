#pragma once

namespace util
{
struct UtmCoordinate
{
    F64 easting;  // Metres, WGS84 UTM (EPSG:326xx / EPSG:327xx).
    F64 northing;
    U32 zone;     // 1 through 60, not a latitude band.
    bool southern_hemisphere; // Zero selects the northern hemisphere.
};

// Returns longitude in x and latitude in y, both in degrees.
g_internal Vec2F64
util_wgs84_from_utm(UtmCoordinate coordinate);
} // namespace util
