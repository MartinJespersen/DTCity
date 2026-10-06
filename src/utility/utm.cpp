namespace util
{
Vec2F64
util_wgs84_from_utm(UtmCoordinate coordinate)
{
    Assert(coordinate.zone >= 1 && coordinate.zone <= 60);

    // Inverse Transverse Mercator, EPSG Guidance Note 7-2 (method 9807).
    // Fourth-order Kruger series with the WGS84 ellipsoid and UTM scale.
    constexpr F64 flattening = 1.0 / 298.257223563;
    constexpr F64 n = flattening / (2.0 - flattening);
    constexpr F64 n2 = n * n;
    constexpr F64 n3 = n2 * n;
    constexpr F64 n4 = n2 * n2;
    constexpr F64 b = 6378137.0 / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0);
    constexpr F64 coefficients[] = {
        n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0 - n4 / 360.0,
        n2 / 48.0 + n3 / 15.0 - 437.0 * n4 / 1440.0,
        17.0 * n3 / 480.0 - 37.0 * n4 / 840.0,
        4397.0 * n4 / 161280.0};

    F64 false_northing = coordinate.southern_hemisphere ? 10000000.0 : 0.0;
    F64 eta = (coordinate.easting - 500000.0) / (b * 0.9996);
    F64 xi = (coordinate.northing - false_northing) / (b * 0.9996);
    F64 eta_zero = eta;
    F64 xi_zero = xi;
    for (U32 i = 0; i < 4; ++i)
    {
        F64 multiple = 2.0 * (i + 1);
        xi_zero -= coefficients[i] * sin(multiple * xi) * cosh(multiple * eta);
        eta_zero -= coefficients[i] * cos(multiple * xi) * sinh(multiple * eta);
    }

    // Recover geodetic latitude from conformal latitude. Fixed iterations keep
    // the conversion bounded; WGS84's small eccentricity converges rapidly.
    F64 eccentricity = sqrt(flattening * (2.0 - flattening));
    F64 beta = asin(sin(xi_zero) / cosh(eta_zero));
    F64 q_prime = asinh(tan(beta));
    F64 q = q_prime;
    for (U32 i = 0; i < 8; ++i)
    {
        q = q_prime + eccentricity * atanh(eccentricity * tanh(q));
    }

    constexpr F64 degrees_per_radian = 180.0 / 3.14159265358979323846;
    F64 central_meridian = 6.0 * coordinate.zone - 183.0;
    F64 longitude = central_meridian + atan2(sinh(eta_zero), cos(xi_zero)) * degrees_per_radian;
    F64 latitude = atan(sinh(q)) * degrees_per_radian;
    // Zones bordering the antimeridian may contain coordinates on either side.
    if (longitude > 180.0)
    {
        longitude -= 360.0;
    }
    else if (longitude < -180.0)
    {
        longitude += 360.0;
    }
    return {longitude, latitude};
}
} // namespace util
