#pragma once

#include "neta.hpp"
#include "road.hpp"
#include "tessellation.hpp"
#include "agent.hpp"
#include "tile_bvh.hpp"
#include "base/base_allocator.hpp"
#include "utility/utm.hpp"

struct gltfw_Sampler;
struct gltfw_Vertex3D;

namespace city
{

static_assert(sizeof(RoadSegmentCorners) == 64, "size of road segment might not match shader size");

enum class AsyncTaskType : U32
{
    None,
    Neta,
    Osm,
    Road,
    CarSim,
};

struct AsyncCityTask
{
    AsyncCityTask* next;
    AsyncCityTask* prev;

    AsyncTaskType type;
    union
    {
        async::AsyncTaskStatus<neta::NetaLoadTask>* neta;
        async::AsyncTaskStatus<osm::NetworkLoadTask>* osm;
        async::AsyncTaskStatus<RoadBuildTask>* road;
        async::AsyncTaskStatus<city::AgentSimThread>* agent_sim_task;
    };
};

struct AsyncCityTaskList
{
    AsyncCityTask* first;
    AsyncCityTask* last;
};

enum class AreaCoordinateType
{
    Wgs84,
    Utm
};

enum class AreaTilesetSource
{
    Terrain, // Zero initialization selects default ion terrain.
    Path,
    IonAsset
};

struct AreaConfig
{
    String8 name;
    AreaCoordinateType coordinate_type;
    Vec2F64 wgs84; // Longitude (x), latitude (y), in degrees.
    util::UtmCoordinate utm;
    F32 bbox_width_meters;
    F32 bbox_height_meters;
    AreaTilesetSource tileset_source;
    String8 tileset_path;
    S64 tileset_ion_asset_id;
    bool bbox_clipping_enabled;
    bool custom_geometry_enabled;
};

Vec2F64
city_area_wgs84_get(const AreaConfig* config);

struct City
{
    Arena* arena;
    Allocator* allocator;
    TileBvh* tile_bvh;

    String8 tileset_url;
    Rng2F64 bbox;
    String8 cache_path;
    bool neta_task_done;
    bool neta_task_failed;
    bool osm_task_done;
    bool osm_task_failed;
    bool road_building_started;
    bool road_building_done;
    bool agent_creation_started;
    bool cars_creation_done;

    bool no_gui_focus;

    PoolHandle road;
    RoadBuildInput road_input;
    AgentSystem* agent_system;
    F32 all_agent_scale_factor;
    PoolHandle neta_state;

    // handles
    PoolHandle osm_network;
    PoolHandle camera_handle;
    PoolHandle tileset_handle;

    // async
    AsyncCityTaskList task_list;
};

// global

g_internal AsyncCityTask*
_cache_and_parse_osm_json(Arena* arena, async::ThreadPool* thread_pool, osm::NetworkCache cache, Rng2F64 bbox);

g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler);

g_internal async::AsyncTaskContinuation<AgentSimThread>
agent_sim_build(async::ThreadInfo info, async::AsyncTaskStatus<AgentSimThread>* status);

void
city_build(City* city, Rng2F64 bbox, String8 tileset_url, String8 area);
void
city_area_streaming_begin(City* city, const AreaConfig* area_config);
B32
city_area_streaming_end(City* city);
void
city_update(City* city, async::ThreadPool* thread_pool, RoadOverlayOption neta_overlay_option, Vec2U32 framebuffer_dim,
            const AreaConfig* city_config);
void
city_init(City* city, String8 cache_path);
void
city_release(City* city);


// coordinates from str list

g_internal Rng3F32
_agent_world_bounds_from_transform(Rng3F32 model_bounds, glm::mat4 model_transform);

// helpers
g_internal void
_city_tasks_collect(City* city, bool publish_results);
} // namespace city
