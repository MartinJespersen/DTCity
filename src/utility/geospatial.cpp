namespace util
{
// builds a wgs84 bounding box from a corner point and a size in metres. the
// given corner becomes the min corner; the opposite (max) corner is found by
// offsetting `width` metres east and `height` metres north on an east-north-up
// tangent frame placed at the corner.
g_internal Rng2F64
wgs84_bbox_from_btm_right_corner(F64 lon, F64 lat, F64 width, F64 height)
{
    CesiumGeospatial::Cartographic corner = CesiumGeospatial::Cartographic::fromDegrees(lon, lat, 0.0);
    CesiumGeospatial::LocalHorizontalCoordinateSystem enu(corner, CesiumGeospatial::LocalDirection::East, CesiumGeospatial::LocalDirection::North,
                                                          CesiumGeospatial::LocalDirection::Up);

    glm::dvec3 opposite_ecef = enu.localPositionToEcef(glm::dvec3(width, height, 0.0));
    std::optional<CesiumGeospatial::Cartographic> opposite = CesiumGeospatial::Ellipsoid::WGS84.cartesianToCartographic(opposite_ecef);

    Rng2F64 wgs84_bbox = {};
    wgs84_bbox.min = {lon, lat};
    if (opposite)
    {
        wgs84_bbox.max = {glm::degrees(opposite->longitude), glm::degrees(opposite->latitude)};
    }
    return wgs84_bbox;
}

// derives the EPSG SRID of the UTM zone that contains the given wgs84 point.
// returns 0 when the longitude is outside the valid wgs84 range.
g_internal S32
target_srid_from_wgs84(Vec2F64 wgs84_point)
{
    if (wgs84_point.x < -180.0 || wgs84_point.x > 180.0)
    {
        return 0;
    }

    // each utm zone spans 6 degrees of longitude starting at -180 degrees
    S32 zone_number = (S32)((wgs84_point.x + 180.0) / 6.0) + 1;
    if (zone_number > 60)
    {
        zone_number = 60;
    }

    // EPSG uses 326xx for the northern hemisphere and 327xx for the southern
    S32 srid_base = wgs84_point.y >= 0.0 ? 32600 : 32700;
    return srid_base + zone_number;
}

} // namespace util
