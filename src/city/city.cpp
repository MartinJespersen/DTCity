#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/cache.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "utility/utility_inc.hpp"
#include "async/async_inc.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/misc_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "gltfw/gltfw.hpp"
#include "osm/osm.hpp"
#include "cesium/cesium_tileset.hpp"
#include "city/city_inc.hpp"
#include "city/city.hpp"
#include "city/tile_bvh.hpp"
#include "base/base_allocator.hpp"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_allocator_templates.hpp"
#include "render/tile_bvh.hpp"
#include "entrypoint.hpp"

namespace city
{

Vec2F64
city_area_wgs84_get(const AreaConfig* config)
{
    switch (config->coordinate_type)
    {
        case AreaCoordinateType::Wgs84: return config->wgs84;
        case AreaCoordinateType::Utm:
        {
            Vec2F64 wgs84 = util::util_wgs84_from_utm(config->utm);
            return wgs84;
        }
        default: InvalidPath;
    }
    return {};
}

void
city_init(City* city, String8 cache_path)
{
    // The city allocator owns the same lifetime as the original city arena.
    city->allocator = new Allocator({arena_default_reserve_size, arena_default_commit_size, arena_default_flags});
    Arena* arena = city->allocator->arena;
    Debug_SetName(arena, "city arena");
    city->cache_path = push_str8_copy(arena, cache_path);
    city->arena = arena;
    city->tile_bvh = city->allocator->make<TileBvh>();
    city->all_agent_scale_factor = 1.0f;
}

void
city_area_streaming_begin(City* city, const AreaConfig* area_config)
{
    Assert(city);
    Assert(area_config);

    String8 tileset_url = {};
    S64 ion_asset_id = 0;
    switch (area_config->tileset_source)
    {
        case AreaTilesetSource::Terrain: break;
        case AreaTilesetSource::Path:
        {
            tileset_url = area_config->tileset_path;
            Assert(tileset_url.size > 0);
        }
        break;
        case AreaTilesetSource::IonAsset:
        {
            ion_asset_id = area_config->tileset_ion_asset_id;
            Assert(ion_asset_id > 0);
        }
        break;
        default: InvalidPath;
    }

    Context* ctx = dt_ctx_get();
    PoolHandle tileset_handle =
        tile_load_streaming_begin(ctx->tile_load_state, tileset_url, city->bbox, ion_asset_id, MB(256));
    city->tileset_handle = tileset_handle;

    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = ctx->tile_load_state->tileset_pool->item_from_handle(tileset_handle, &tileset);
    if (tileset_exists)
        tileset->tile_bvh = city->tile_bvh;
    if (city->cars_creation_done && tileset_exists)
    {
        city->agent_system->_agent_height_updates_start(tileset);
    }
}

B32
city_area_streaming_end(City* city)
{
    Assert(city);

    Context* ctx = dt_ctx_get();
    if (city->cars_creation_done)
    {
        city->agent_system->_agent_height_updates_stop();
    }
    B32 destroyed = tile_load_streaming_end(ctx->tile_load_state, city->tileset_handle);
    return destroyed;
}

void
city_build(City* city, Rng2F64 bbox, String8 tileset_url, String8 area)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Context* ctx = dt_ctx_get();

    city->bbox = bbox;
    city->tileset_url = push_str8_copy(city->arena, tileset_url);

    // cache str create
    String8List bbox_param_list = {};
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.min.y));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.min.x));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.max.y));
    str8_list_push(scratch.arena, &bbox_param_list, push_str8f(scratch.arena, "%.6f", bbox.max.x));
    StringJoin sep = {.pre = S("bbox_str="), .sep = S(","), .post = S("")};
    String8 bbox_cache_str = str8_list_join(city->arena, &bbox_param_list, &sep);

    // cesium init
    Vec2F64 bbox_center = {.x = (bbox.min.x + bbox.max.x) * 0.5, .y = (bbox.min.y + bbox.max.y) * 0.5};
    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(bbox_center.x), glm::radians(bbox_center.y), 0);
    CesiumGeospatial::LocalHorizontalCoordinateSystem local_coord = CesiumGeospatial::LocalHorizontalCoordinateSystem(
        origin_cartographic, CesiumGeospatial::LocalDirection::East, CesiumGeospatial::LocalDirection::North,
        CesiumGeospatial::LocalDirection::Up);
    glm::dmat4 ecef_to_local = local_coord.getEcefToLocalTransformation();

    city->road_input = {.bbox = bbox, .ecef_to_local = ecef_to_local,
                        .default_road_width = 2.0f};

    // Both load tasks own copies of their request inputs and publish results on completion.
    neta::NetaCache neta_cache = {
        .file_location = str8_path_from_str8_list(scratch.arena,
                                                  {ctx->data_subdirs.data[dt_DataDirType::Cache], area, S("netascore_edges.geojson")}),
        .bbox_str = bbox_cache_str,
    };
    AsyncCityTask* neta_task = PushStruct(city->arena, AsyncCityTask);
    neta_task->type = AsyncTaskType::Neta;
    neta_task->neta = neta::neta_load(ctx->thread_pool, neta_cache, bbox);
    DLLPushBack(city->task_list.first, city->task_list.last, neta_task);

    async::ThreadPool* thread_pool = dt_ctx_get()->thread_pool;
    osm::NetworkCache cache = {
        .file_location = str8_path_from_str8_list(scratch.arena,
                                                  {ctx->data_subdirs.data[dt_DataDirType::Cache], area, S("osm_data.json")}),
        .bbox_str = bbox_cache_str,
    };
    AsyncCityTask* osm_task_status = _cache_and_parse_osm_json(city->allocator->arena, thread_pool, cache, bbox);
    DLLPushBack(city->task_list.first, city->task_list.last, osm_task_status);
}

g_internal AsyncCityTask*
_cache_and_parse_osm_json(Arena* arena, async::ThreadPool* thread_pool, osm::NetworkCache cache, Rng2F64 bbox)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8List query_list = {};
    for (U32 way_type_idx = 0; way_type_idx < enum_idx(osm::WayType::Count); ++way_type_idx)
    {
        String8 str = PushStr8F(scratch.arena, "way[\"%s\"](%f, %f, %f, %f);\n", osm::g_waytype_osm_tag[way_type_idx],
                                bbox.min.y, bbox.min.x, bbox.max.y, bbox.max.x);
        str8_list_push(scratch.arena, &query_list, str);
    }

    String8 dyn_query_str = str8_list_join(scratch.arena, &query_list, 0);
    // fetch osm data from overpass api
    String8 body = push_str8f(scratch.arena, R"(data=
            [out:json] [timeout:25];
            (
                %.*s
            );
            out body;
            >;
            out skel qt;
        )",
                              str8_varg(dyn_query_str));

    AsyncCityTask* osm_task = PushStruct(arena, AsyncCityTask);
    osm_task->type = city::AsyncTaskType::Osm;
    osm_task->osm = osm::network_load(thread_pool, cache, body);
    return osm_task;
}


void
city_update(City* city, async::ThreadPool* thread_pool, RoadOverlayOption neta_overlay_option, Vec2U32 framebuffer_dim,
            const AreaConfig* city_config)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    Context* ctx = dt_ctx_get();
    draw::DrawFrame* draw_frame = draw::draw_frame_get();

    _city_tasks_collect(city, true);

    osm::Network* osm_network = {};
    bool network_ready = ctx->osm_network_pool->item_from_handle(city->osm_network, &osm_network);
    Assert(!city->osm_task_done || network_ready);

    U64 hovered_object_id = render::latest_hovered_object_id_get();

    if (city->neta_task_done && city->osm_task_done && !city->road_building_started)
    {
        city->road_building_started = true;
        DEBUG_LOG("City building work has been schedule");
        neta::NetaState* neta = {};
        B32 neta_exists = ctx->neta_state_pool->item_from_handle(city->neta_state, &neta);
        Assert(neta_exists);
        RoadBuildInput input = city->road_input;
        input.network = osm_network;
        input.netascore_file_path = neta->cache_file_location;
        async::AsyncTaskStatus<RoadBuildTask>* road_building_task =
            ctx->road_context->create(thread_pool, input);
        AsyncCityTask* road_task_list_elem = PushStruct(city->arena, AsyncCityTask);
        road_task_list_elem->type = AsyncTaskType::Road;
        road_task_list_elem->road = road_building_task;
        DLLPushBack(city->task_list.first, city->task_list.last, road_task_list_elem);
    }

    // input:
    // - Bounding box (in 3D to include height)
    //  - Using local coordinates from cesium library?
    // - texture handle of texture to use
    //

    Road* road = {};
    bool road_exists = ctx->road_context->get(city->road, &road);
    B32 road_overlay_changed = road_exists && road->overlay_option_cur != neta_overlay_option;
    city->road_building_done = ctx->road_context->update(city->road, neta_overlay_option);
    PoolHandle road_bvh = road_exists ? road->bvh_handle : PoolHandle{};
    F64 delta_time = ctx->time->time_delta_constant_sec;
    tile_load_update(ctx->tile_load_state, city->tileset_handle, road_bvh,
                     draw_frame->camera_resource_handle, city->road_building_done, road_overlay_changed,
                     neta_overlay_option, delta_time);

    // Tile completion callbacks only queue registry changes. Advance the hierarchy
    // once per frame, including frames with no new entries, without waiting.
    render::TileBvhHandle ready_tile_bvh = city->tile_bvh->update({}, {});

    // Update and render Cesium 3D Tiles ////////////
    cesium::TilesetRenderer* tileset = {};
    B32 tileset_exists = ctx->tile_load_state->tileset_pool->item_from_handle(city->tileset_handle, &tileset);
    if (tileset_exists && !tileset->destruction_requested)
    {
        tileset->ready_tile_bvh = ready_tile_bvh;

        if (city->osm_task_done)
        {
            if (!city->no_gui_focus)
            {
                ImGui::PushFont(nullptr, 18.0f);
                osm::Edge** edge_ptr = osm_network->edge_structure.edge_map.get((S64)hovered_object_id);
                if (edge_ptr)
                {
                    osm::Edge* edge = *edge_ptr;
                    osm::WayNode* way_node = osm::way_find(osm_network, edge->way_id);
                    osm::Way* way = &way_node->way;

                    bool open = true;
                    ImGuiWindowFlags object_info_flags = ImGuiWindowFlags_AlwaysAutoResize;
                    ImGui::PushFont(nullptr, 22.0f);
                    ImGui::Begin("Road Info", &open, object_info_flags);
                    ImGui::PopFont();

                    for (osm::Tag& tag : way->tags)
                    {
                        ImGui::Text("%s: %s", (char*)tag.key.str, (char*)tag.value.str);
                    }

                    city::RoadInfo* chosen_edge = road_exists ? road->road_info_map->get(edge->id) : nullptr;
                    if (chosen_edge)
                    {
                        for (U32 i = 1; i < ArrayCount(chosen_edge->options); i++)
                        {
                            ImGui::Text("%s: %lf", city::road_overlay_option_strs[i], chosen_edge->options[i]);
                        }
                    }
                    ImVec2 window_size = ImGui::GetWindowSize();
                    ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
                    ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

                    ImGui::End();
                }
                osm::WayNode* way_node = osm::way_find(osm_network, hovered_object_id);
                if (way_node)
                {
                    osm::Way* way = &way_node->way;
                    bool open = true;
                    ImGuiWindowFlags object_info_flags = ImGuiWindowFlags_AlwaysAutoResize |
                                                         ImGuiWindowFlags_NoMouseInputs |
                                                         ImGuiWindowFlags_NoFocusOnAppearing;

                    ImGui::PushFont(nullptr, 22.0f);
                    ImGui::Begin("Road Info", &open, object_info_flags);
                    ImGui::PopFont();
                    for (U32 tag_idx = 0; tag_idx < way->tags.size; tag_idx += 1)
                    {
                        osm::Tag* tag = &way->tags.data[tag_idx];
                        ImGui::Text("%s: %s", (char*)tag->key.str, (char*)tag->value.str);
                    }
                    ImVec2 window_size = ImGui::GetWindowSize();
                    ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
                    ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

                    ImGui::End();
                }
                ImGui::PopFont();
            }
        }
        // always drawn tiles
        for (cesium::TileRenderResources* tile = tileset->tile_to_show_first; tile; tile = tile->render_next)
        {
            for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
            {
                B32 is_map_tile = has_flag(batch->render_data.pipeline_bits, render::TilePipelineBits::IsMapTile);
                if (is_map_tile)
                {
                    if (city_config->bbox_clipping_enabled)
                    {
                        batch->render_data.height_offset = -tileset->height_offset;
                    }

                    ctx->road_context->pipeline_add(city->road, &batch->render_data, neta_overlay_option);
                }
            }
        }

        // custom tile draw
        {
            prof_scope_marker_named("custom tile draw scope");
            // custom tile depth only with depth compare always
            for (cesium::TileRenderResources* tile = tileset->tile_to_show_first; tile; tile = tile->render_next)
            {
                for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
                {
                    B32 is_custom_tile =
                        has_flag(batch->render_data.pipeline_bits, render::TilePipelineBits::IsMapTile) == false;
                    if (is_custom_tile)
                    {
                        batch->render_data.pipeline_bits |= render::TilePipelineBits::ColorDisable;
                        batch->render_data.depth_test_compare = render::DepthCompare::Always;
                        ctx->road_context->pipeline_add(city->road, &batch->render_data, neta_overlay_option);
                    }
                }
            }
            // draw custom tiles with depth diabled but color enabled
            for (cesium::TileRenderResources* tile = tileset->tile_to_show_first; tile; tile = tile->render_next)
            {
                for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
                {
                    B32 is_custom_tile =
                        has_flag(batch->render_data.pipeline_bits, render::TilePipelineBits::IsMapTile) == false;
                    if (is_custom_tile)
                    {
                        batch->render_data.pipeline_bits &= (~render::TilePipelineBits::ColorDisable);
                        batch->render_data.depth_test_compare = render::DepthCompare::LessOrEqual;
                        ctx->road_context->pipeline_add(city->road, &batch->render_data, neta_overlay_option);
                    }
                }
            }
        }

        if (city->agent_creation_started == false && city->osm_task_done)
        {
            Debug_SetName(tileset->allocator.arena, "Cesium Tileset Allocator arena");
            AgentSimThread* agent_sim_thread = new AgentSimThread{};
            AgentSimConfig* agent_sim_config = &agent_sim_thread->config;
            agent_sim_config->asset_dir = push_str8_copy(city->arena, ctx->data_subdirs.data[dt_DataDirType::Assets]);
            agent_sim_config->texture_dir =
                push_str8_copy(city->arena, ctx->data_subdirs.data[dt_DataDirType::Texture]);
            agent_sim_config->agent_map_bucket_count = 100;
            agent_sim_config->max_agent_count = 10000;
            agent_sim_config->agent_config[enum_class_s32(AgentType::Car)] = {
                .asset_file_name = S("car.glb"), .model_forward_dir = glm::vec3(0.0, 0.0, 1.0)};
            agent_sim_config->agent_config[enum_class_s32(AgentType::Bicycle)] = {.asset_file_name = S("bike.glb"),
                                                                                  .model_forward_dir =
                                                                                      glm::vec3(-1.0, 0.0, 0.0),
                                                                                  .model_to_world_scale = 1.0};
            async::AsyncTaskStatus<AgentSimThread>* agent_task_state =
                async::async_task_run(ctx->thread_pool, agent_sim_build, agent_sim_thread, "Car Sim Task");

            AsyncCityTask* car_sim_task_list_elem = PushStruct(city->arena, AsyncCityTask);
            car_sim_task_list_elem->type = AsyncTaskType::CarSim;
            car_sim_task_list_elem->agent_sim_task = agent_task_state;
            DLLPushBack(city->task_list.first, city->task_list.last, car_sim_task_list_elem);

            city->agent_creation_started = true;
        }
    }
}

void
city_release(City* city)
{
    // Final shutdown has no frame loop, so drain GPU work explicitly here.
    B32 streaming_ended = false;
    while (!streaming_ended || city->task_list.first)
    {
        async::thread_pool_main_thread_queue_drain(dt_ctx_get()->thread_pool);
        render::gpu_work_update();
        streaming_ended = city_area_streaming_end(city);
        // Road tasks borrow the network until completion. Discard late results
        // before releasing either the network or the city task list.
        _city_tasks_collect(city, false);
    }
    Assert(!city->task_list.first);
    dt_ctx_get()->road_context->release(city->road, dt_ctx_get()->tile_load_state->polygon_bvh_pool);
    city->road = {};
    // Releasing the slot also destroys its network and allocator.
    auto* network_pool = dt_ctx_get()->osm_network_pool;
    osm::Network* network = {};
    if (network_pool->item_from_handle(city->osm_network, &network))
    {
        network_pool->item_free(city->osm_network);
        city->osm_network = {};
    }

    delete city->agent_system;
    city->agent_system = {};

    dt_ctx_get()->neta_state_pool->item_free(city->neta_state);
    city->neta_state = {};
    if (city->tile_bvh)
    {
        // Pending tile reprocessing can drain after this city is released. Detach
        // callbacks before releasing the registry; worker BVH snapshots own their buffers.
        Context* ctx = dt_ctx_get();
        for (cesium::TileRenderResources* tile = ctx->tile_load_state->tile_first; tile; tile = tile->next)
        {
            if (tile->tile_bvh == city->tile_bvh)
                tile->tile_bvh = {};
        }
        city->tile_bvh->destroy();
        city->tile_bvh = {};
    }
    delete city->allocator;
    city->allocator = {};
    city->arena = {};
}

g_internal async::AsyncTaskContinuation<AgentSimThread>
agent_sim_build(async::ThreadInfo info, async::AsyncTaskStatus<AgentSimThread>* status)
{
    (void)info;
    city::agents_create(status->user_data);
    return {};
}
g_internal Rng3F32
_agent_world_bounds_from_transform(Rng3F32 model_bounds, glm::mat4 model_transform)
{
    Rng3F32 world_bounds = {};
    for (U32 corner_idx = 0; corner_idx < 8; corner_idx++)
    {
        glm::vec4 model_corner = glm::vec4(corner_idx & 1 ? model_bounds.max.x : model_bounds.min.x,
                                           corner_idx & 2 ? model_bounds.max.y : model_bounds.min.y,
                                           corner_idx & 4 ? model_bounds.max.z : model_bounds.min.z, 1.0f);
        glm::vec4 world_corner = model_transform * model_corner;
        Vec3F32 world_pos = {world_corner.x, world_corner.y, world_corner.z};
        if (corner_idx == 0)
        {
            world_bounds = r3f32(world_pos, world_pos);
        }
        else
        {
            world_bounds.min.x = Min(world_bounds.min.x, world_pos.x);
            world_bounds.min.y = Min(world_bounds.min.y, world_pos.y);
            world_bounds.min.z = Min(world_bounds.min.z, world_pos.z);
            world_bounds.max.x = Max(world_bounds.max.x, world_pos.x);
            world_bounds.max.y = Max(world_bounds.max.y, world_pos.y);
            world_bounds.max.z = Max(world_bounds.max.z, world_pos.z);
        }
    }
    return world_bounds;
}

g_internal render::SamplerInfo
sampler_from_cgltf_sampler(gltfw_Sampler sampler)
{
    render::Filter min_filter = render::Filter_Nearest;
    render::Filter mag_filter = render::Filter_Nearest;
    render::MipMapMode mipmap_mode = render::MipMapMode_Nearest;
    render::SamplerAddressMode address_mode_u = render::SamplerAddressMode_Repeat;
    render::SamplerAddressMode address_mode_v = render::SamplerAddressMode_Repeat;

    switch (sampler.min_filter)
    {
        default: break;
        case cgltf_filter_type_nearest:
        {
            min_filter = render::Filter_Nearest;
            mag_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            min_filter = render::Filter_Nearest;
            mag_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            mag_filter = render::Filter_Nearest;
            min_filter = render::Filter_Nearest;
            mipmap_mode = render::MipMapMode_Linear;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            min_filter = render::Filter_Linear;
            mag_filter = render::Filter_Linear;
            mipmap_mode = render::MipMapMode_Linear;
        }
        break;
    }

    if (sampler.mag_filter != sampler.min_filter)
    {
        switch (sampler.mag_filter)
        {
            default: break;
            case cgltf_filter_type_nearest_mipmap_nearest:
            {
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_nearest:
            {
                mag_filter = render::Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest_mipmap_linear:
            {
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_linear:
            {
                mag_filter = render::Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest:
            {
                mag_filter = render::Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear:
            {
                mag_filter = render::Filter_Linear;
            }
            break;

                break;
        }
    }

    switch (sampler.wrap_s)
    {
        case cgltf_wrap_mode_clamp_to_edge: address_mode_u = render::SamplerAddressMode_ClampToEdge; break;
        case cgltf_wrap_mode_repeat: address_mode_u = render::SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat: address_mode_u = render::SamplerAddressMode_MirroredRepeat; break;
    }
    switch (sampler.wrap_t)
    {
        case cgltf_wrap_mode_clamp_to_edge: address_mode_v = render::SamplerAddressMode_ClampToEdge; break;
        case cgltf_wrap_mode_repeat: address_mode_v = render::SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat: address_mode_v = render::SamplerAddressMode_MirroredRepeat; break;
    }

    render::SamplerInfo sampler_info = {.min_filter = min_filter,
                                        .mag_filter = mag_filter,
                                        .mip_map_mode = mipmap_mode,
                                        .address_mode_u = address_mode_u,
                                        .address_mode_v = address_mode_v};

    return sampler_info;
}

g_internal void
_city_tasks_collect(City* city, bool publish_results)
{
    for (AsyncCityTask* task = city->task_list.first; task;)
    {
        AsyncCityTask* next_task = task->next;
        bool task_done = false;
        switch (task->type)
        {
            case AsyncTaskType::Osm:
            {
                if (auto completed = async::async_task_take_completed(task->osm))
                {
                    task_done = true;
                    city->osm_task_failed = completed->error;
                    city->osm_task_done = !completed->error;
                    if (!completed->error && publish_results)
                    {
                        city->osm_network = dt_ctx_get()->osm_network_pool->emplace(
                            std::move(*completed->user_data->result));
                    }
                }
            };
            break;
            case AsyncTaskType::Neta:
            {
                if (auto completed = async::async_task_take_completed(task->neta))
                {
                    task_done = true;
                    city->neta_task_failed = completed->error;
                    city->neta_task_done = !completed->error;
                    if (!completed->error && publish_results)
                    {
                        auto& result = completed->user_data->result;
                        city->neta_state = dt_ctx_get()->neta_state_pool->emplace(std::move(*result));
                    }
                }
            }
            break;
            case AsyncTaskType::Road:
            {
                city->road_building_done = false;
                if (auto completed = async::async_task_take_completed(task->road))
                {
                    task_done = true;
                    if (!completed->error && publish_results)
                    {
                        Context* ctx = dt_ctx_get();
                        city->road = ctx->road_context->publish(completed->user_data,
                            ctx->tile_load_state->polygon_bvh_pool);
                    }
                }
            }
            break;
            case AsyncTaskType::CarSim:
            {
                if (publish_results) city->cars_creation_done = false;
                if (auto completed = async::async_task_take_completed(task->agent_sim_task))
                {
                    task_done = true;
                    if (publish_results) city->cars_creation_done = !completed->error;
                    AgentSimThread* agent_sim_thread = completed->user_data;
                    if (!completed->error && publish_results)
                    {
                        Assert(!city->agent_system);

                        city->agent_system = new AgentSystem{};
                        city->agent_system->agent_sim_config =
                            std::make_unique<AgentSimConfig>(std::move(agent_sim_thread->config));
                        city->agent_system->agent_sim =
                            std::make_unique<AgentSim>(city->agent_system->agent_sim_config->agent_map_bucket_count,
                                                       city->agent_system->agent_sim_config->max_agent_count);
                    }
                    delete agent_sim_thread;
                }
            }
            break;
            case AsyncTaskType::None: InvalidPath; break;
        }
        if (task_done)
        {
            DLLRemove(city->task_list.first, city->task_list.last, task);
        }
        task = next_task;
    }
}

} // namespace city
