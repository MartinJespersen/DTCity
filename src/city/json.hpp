#pragma once

// Propagate the first parsing error from functions returning simdjson::error_code.
#define JSON_TRY(expression)                             \
    do                                                   \
    {                                                    \
        simdjson::error_code json_error_ = (expression); \
        if (json_error_)                                 \
        {                                                \
            return json_error_;                          \
        }                                                \
    } while (0)

namespace city
{
struct CoordinateView
{
    std::string_view id;
    F64 time;
    U64 event_type;
    U64 node_from_id;
    U64 node_to_id;
    F64 lon_from;
    F64 lat_from;
    F64 lon_to;
    F64 lat_to;
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
    F64 time;
    S64 event_type;
    S64 node_from_id;
    S64 node_to_id;
    glm::dvec2 from;
    glm::dvec2 to;
};

struct CoordinateBatch
{
    Buffer<Coordinate> coordinates;
    B32 reset;
};

g_internal void
simulator_coordinate_batch_append(Arena* arena, CoordinateBatch* batch, Buffer<Coordinate> incoming, B32 reset);

} // namespace city

template <>
simdjson_inline simdjson::error_code
simdjson::ondemand::value::get(city::CoordinateView& out) noexcept
{
    simdjson::ondemand::object obj;
    JSON_TRY(get_object().get(obj));
    JSON_TRY(obj["id"].get_string().get(out.id));
    JSON_TRY(obj["time"].get_double().get(out.time));
    JSON_TRY(obj["event_type"].get_uint64().get(out.event_type));
    JSON_TRY(obj["node_from_id"].get_uint64().get(out.node_from_id));
    JSON_TRY(obj["node_to_id"].get_uint64().get(out.node_to_id));
    JSON_TRY(obj["lon_from"].get_double().get(out.lon_from));
    JSON_TRY(obj["lat_from"].get_double().get(out.lat_from));
    JSON_TRY(obj["lon_to"].get_double().get(out.lon_to));
    JSON_TRY(obj["lat_to"].get_double().get(out.lat_to));

    return simdjson::SUCCESS;
}
