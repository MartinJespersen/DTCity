// Standalone tests for the dependency-free coordinate conversion.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest/doctest.h"
#include <memory>
#include "base/base_context_cracking.h"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_math.h"
#include "utility/utm.hpp"
#include "utility/utm.cpp"

TEST_CASE("UTM central meridians and hemisphere false northing")
{
    for (U32 zone = 1; zone <= 60; ++zone)
    {
        Vec2F64 north = util::util_wgs84_from_utm({500000.0, 0.0, zone, false});
        Vec2F64 south = util::util_wgs84_from_utm({500000.0, 10000000.0, zone, true});
        CHECK(north.x == doctest::Approx(6.0 * zone - 183.0));
        CHECK(north.y == doctest::Approx(0.0));
        CHECK(south.x == doctest::Approx(north.x));
        CHECK(south.y == doctest::Approx(0.0));
    }
}

TEST_CASE("UTM inversion matches independently projected WGS84 coordinates")
{
    // Reference eastings/northings generated with PROJ through pyproj 3.7.2.
    // Absolute tolerance 1e-7 degrees is approximately one centimetre in latitude.
    Vec2F64 aarhus = util::util_wgs84_from_utm({580000.0094951922, 6234999.995356444, 32, false});
    CHECK(fabs(aarhus.x - 10.291206) < 1e-7);
    CHECK(fabs(aarhus.y - 56.253108) < 1e-7);

    Vec2F64 sydney = util::util_wgs84_from_utm({334368.633648097, 6250948.345385009, 56, true});
    CHECK(fabs(sydney.x - 151.2093) < 1e-7);
    CHECK(fabs(sydney.y + 33.8688) < 1e-7);

    Vec2F64 svalbard = util::util_wgs84_from_utm({615914.5248767398, 8663320.20140382, 33, false});
    CHECK(fabs(svalbard.x - 20.0) < 1e-7);
    CHECK(fabs(svalbard.y - 78.0) < 1e-7);

    Vec2F64 southern_edge = util::util_wgs84_from_utm({436123.8375445354, 1228384.0340933576, 31, true});
    CHECK(fabs(southern_edge.x) < 1e-7);
    CHECK(fabs(southern_edge.y + 79.0) < 1e-7);

    Vec2F64 equator = util::util_wgs84_from_utm({166021.44308054057, 0.0, 31, false});
    CHECK(fabs(equator.x) < 1e-7);
    CHECK(fabs(equator.y) < 1e-7);

    Vec2F64 antimeridian = util::util_wgs84_from_utm({822836.1940437458, 0.0, 60, false});
    CHECK(fabs(antimeridian.x - 179.9) < 1e-7);
    CHECK(fabs(antimeridian.y) < 1e-7);
}
