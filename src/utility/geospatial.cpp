namespace util
{
g_internal Vec2F64
wgs84_from_utm(Vec2F64 utm_point, String8 utm_zone)
{
    if (utm_zone.size == 0)
        return {};

    F64 lon;
    F64 lat;
    UTM::UTMtoLL(utm_point.y, utm_point.x, (char*)utm_zone.str, lat, lon);

    return {lon, lat};
}

g_internal Vec2F64
default_bbox_size_meters_get()
{
    return vec_2f64(5000.0, 5000.0);
}

g_internal Vec2F64
utm_point_from_wgs84(Vec2F64 wgs84_point, char out_utm_zone[10])
{
    F64 northing = 0;
    F64 easting = 0;
    UTM::LLtoUTM(wgs84_point.y, wgs84_point.x, northing, easting, out_utm_zone);
    return vec_2f64(easting, northing);
}

g_internal Rng2F64
wgs84_bbox_from_btm_right_corner(String8List* cmdline, Vec2F64 bbox_size_meters)
{
    ScratchScope scratch = ScratchScope(0, 0);
    // TODO: Debug log this
    String8 lon = os_arg_from_cmdline(scratch.arena, cmdline, S("lon"));
    String8 lat = os_arg_from_cmdline(scratch.arena, cmdline, S("lat"));
    String8 zone = os_arg_from_cmdline(scratch.arena, cmdline, S("zone"));

    Vec2F64 point = {f64_from_str8(lon), f64_from_str8(lat)};
    char utm_zone[10] = {};
    Vec2F64 btm_right_utm = utm_point_from_wgs84(point, utm_zone);

    Rng2F64 utm_bbox = {};
    utm_bbox.min.x = btm_right_utm.x;
    utm_bbox.min.y = btm_right_utm.y;
    utm_bbox.max.x = btm_right_utm.x + bbox_size_meters.x;
    utm_bbox.max.y = btm_right_utm.y + bbox_size_meters.y;

    Rng2F64 wgs84_bbox = {};
    wgs84_bbox.min = wgs84_from_utm(utm_bbox.min, zone);
    wgs84_bbox.max = wgs84_from_utm(utm_bbox.max, zone);
    return wgs84_bbox;
}

g_internal Rng2F64
utm_from_wgs84(Rng2F64 wgs84_bbox)
{
    F64 south_west_easting;
    F64 south_west_northing;
    F64 north_east_easting;
    F64 north_east_northing;
    char utm_zone[10] = {};
    UTM::LLtoUTM(wgs84_bbox.min.y, wgs84_bbox.min.x, south_west_northing, south_west_easting, utm_zone);
    UTM::LLtoUTM(wgs84_bbox.max.y, wgs84_bbox.max.x, north_east_northing, north_east_easting, utm_zone);

    Rng2F64 utm_bbox = {};
    utm_bbox.min = vec_2f64(south_west_easting, south_west_northing);
    utm_bbox.max = vec_2f64(north_east_easting, north_east_northing);
    return utm_bbox;
}

static S32
target_srid_from_wgs84(Vec2F64 wgs84_point)
{
    char utm_zone[10] = {};
    utm_point_from_wgs84(wgs84_point, utm_zone);

    S32 zone_number = 0;
    for (char* c = utm_zone; *c >= '0' && *c <= '9'; c += 1)
    {
        zone_number = zone_number * 10 + (*c - '0');
    }

    if (zone_number < 1 || zone_number > 60)
    {
        return 0;
    }

    S32 srid_base = wgs84_point.y >= 0 ? 32600 : 32700;
    return srid_base + zone_number;
}

} // namespace util
