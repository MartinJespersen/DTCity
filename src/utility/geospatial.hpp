namespace util
{
g_internal Vec2F64
wgs84_from_utm(Vec2F64 utm_point, String8 zone);

g_internal Vec2F64
default_bbox_size_meters_get();

g_internal Vec2F64
utm_point_from_wgs84(Vec2F64 wgs84_point, char out_utm_zone[10]);

g_internal Rng2F64
wgs84_bbox_from_btm_right_corner(String8List* cmdline, Vec2F64 bbox_size_meters);

g_internal Rng2F64
utm_from_wgs84(Rng2F64 wgs84_bbox);

g_internal S32
target_srid_from_wgs84(Vec2F64 wgs84_point);

} // namespace util
