void
OSM_TileToLatLon(int z, int x, int y, double* lat_deg, double* lon_deg)
{
    double n = pow(2.0, z);

    *lon_deg = x / n * 360.0 - 180.0;

    double lat_rad = atan(sinh(pi32 * (1 - 2.0 * y / n)));
    *lat_deg = lat_rad * 180.0 / pi32;
}
