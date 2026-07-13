#pragma once

namespace city
{
struct CoordinateView
{
    std::string_view id;
    F64 lat;
    F64 lon;
};
enum class VehicleType : S32
{
    Car,
    Bicycle,
    Count
};

struct Coordinate
{
    S64 id;
    VehicleType vehicle_type;
    F64 lat;
    F64 lon;
};
} // namespace city

template <>
simdjson_inline simdjson::error_code
simdjson::ondemand::value::get(city::CoordinateView& out) noexcept
{
    simdjson::ondemand::object obj;
    simdjson::error_code error = get_object().get(obj);
    if (error)
    {
        return error;
    }

    error = obj["id"].get_string().get(out.id);
    if (error)
    {
        return error;
    }

    error = obj["lat"].get_double().get(out.lat);
    if (error)
    {
        return error;
    }

    error = obj["lon"].get_double().get(out.lon);
    if (error)
    {
        return error;
    }

    return simdjson::SUCCESS;
}
