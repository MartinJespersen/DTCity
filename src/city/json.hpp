#pragma once

namespace city
{
struct Coordinate
{
    S64 id;
    F64 lat;
    F64 lon;
};
} // namespace city

template <>
simdjson_inline simdjson::error_code
simdjson::ondemand::value::get(city::Coordinate& out) noexcept
{
    simdjson::ondemand::object obj;
    simdjson::error_code error = get_object().get(obj);
    if (error)
    {
        return error;
    }

    error = obj["id"].get_int64().get(out.id);
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
