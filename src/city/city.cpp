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
    ArrayResourcePoolHandle tileset_handle =
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

    // road init
    Road* road = &city->road;

    city::road_create(city, road, ecef_to_local, area, bbox_cache_str);
    road->bbox = bbox;

    // init neta layer
    Arena* arena = arena_alloc();
    Debug_SetName(arena, "city neta arena");
    city->neta_state = PushStruct(arena, neta::NetaState);
    neta::NetaState* neta_state = city->neta_state;
    neta_state->arena = arena;
    neta::neta_init(neta_state, ctx->data_subdirs.data[dt_DataDirType::Cache], area, bbox, bbox_cache_str);
    road->netascore_file_path = push_str8_copy(road->arena, neta_state->cache_file_location);

    // start async tasks
    city::AsyncCityTask* neta_task = neta::netascore_async_task_create(city->arena, neta_state, bbox);
    DLLPushBack(city->task_list.first, city->task_list.last, neta_task);

    async::ThreadPool* thread_pool = dt_ctx_get()->thread_pool;
    AsyncCityTask* osm_task_status = _cache_and_parse_osm_json(thread_pool, road, city->osm_network);
    DLLPushBack(city->task_list.first, city->task_list.last, osm_task_status);
}

g_internal AsyncCityTask*
_cache_and_parse_osm_json(async::ThreadPool* thread_pool, Road* road, osm::Network* osm_network)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8List query_list = {};
    for (U32 way_type_idx = 0; way_type_idx < enum_idx(osm::WayType::Count); ++way_type_idx)
    {
        String8 str = PushStr8F(scratch.arena, "way[\"%s\"](%f, %f, %f, %f);\n", osm::g_waytype_osm_tag[way_type_idx],
                                road->bbox.min.y, road->bbox.min.x, road->bbox.max.y, road->bbox.max.x);
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

    String8 path = S("http://overpass-api.de/api/interpreter");
    Result<String8> cache_result =
        cache_read(osm_network->arena, osm_network->cache_file_location, osm_network->bbox_cache_str);

    AsyncCityTask* osm_task = PushStruct(road->arena, AsyncCityTask);
    osm_task->type = city::AsyncTaskType::Osm;
    if (cache_result.err)
    {
        async::HttpInfo* http_info =
            async::http_info_create(osm_network->arena, HTTP_Method_Post, path, S("application/x-www-form-urlencoded"),
                                    {S("User-Agent: DTCity/0.1"), S("Accept: application/json")}, {});
        http_info->body = push_str8_copy(osm_network->arena, body);
        async::AsyncHttpTaskStateConfig<osm::Network> config =
            async::AsyncHttpTaskStateConfig<osm::Network>(osm::fetch_osm_data_and_parse, osm_network, 3, 1);
        async::AsyncHttpTaskCreateResult<osm::Network> http_task_result =
            async::async_http_task_run(thread_pool, http_info, &config, "Osm Task");
        AssertAlways(http_task_result.async_result.has_error() == false);

        osm_task->osm = http_task_result.task_state;
    }
    else
    {
        async::AsyncTaskStatus<osm::Network>* osm_task_cached =
            async::async_task_run(thread_pool, osm::parse_osm_data, osm_network, "Osm Task Http Cached");
        osm_task->osm = osm_task_cached;
    }

    // parse nodes
    // ~mgj: parse OSM way structures
    return osm_task;
}

g_internal void
_tile_pipeline_add(cesium::TileDrawBatch* tile, City* city, ArrayResourcePoolHandle camera_handle,
                   RoadOverlayOption road_overlay_option_idx)
{
    if (city->road.overlay_option_cur != 0)
    {
        tile->render_data.pipeline_bits |= render::TilePipelineBits::ColormapEnable;
    }
    else
    {
        tile->render_data.pipeline_bits &= ~render::TilePipelineBits::ColormapEnable;
    }
    tile->render_data.colormap_handle = city->road.colormap_handle;
    tile->render_data.road_segment_buffer_handle = city->road.segment_buffer_handle;
    tile->render_data.road_test_enabled = city->road_building_done && road_overlay_option_idx != RoadOverlayOption_None;
    tile->render_data.overlay_option_idx = road_overlay_option_idx;
    render::tile_pipeline_add(&tile->render_data);
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

    U64 hovered_object_id = render::latest_hovered_object_id_get();

    if (city->neta_task_done && city->osm_task_done && !city->road_building_started)
    {
        city->road_building_started = true;
        DEBUG_LOG("City building work has been schedule");
        RoadBuildTask* road_build_task = PushStruct(city->arena, RoadBuildTask);
        road_build_task->road = &city->road;
        road_build_task->network = city->osm_network;
        city::RoadBvh* bvh = {};
        if (ctx->tile_load_state->polygon_bvh_pool->item_from_handle(city->road.bvh_handle, &bvh))
        {
            road_build_task->bvh = bvh;
        }
        else
        {
            InvalidPath;
        }

        async::AsyncTaskStatus<RoadBuildTask>* road_building_task =
            async::async_task_run(thread_pool, road_build, road_build_task, "Road Building Task");
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

    B32 road_overlay_changed = city->road.overlay_option_cur != neta_overlay_option;
    F64 delta_time = ctx->time->time_delta_constant_sec;
    tile_load_update(ctx->tile_load_state, city->tileset_handle, city->road.bvh_handle,
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
        if (city->road_building_done)
        {
            city->road.overlay_option_cur = neta_overlay_option;
        }

        if (city->osm_task_done)
        {
            if (!city->no_gui_focus)
            {
                ImGui::PushFont(nullptr, 18.0f);
                osm::RoadEdge** edge_ptr = city->osm_network->edge_structure.edge_map.get((S64)hovered_object_id);
                if (edge_ptr)
                {
                    osm::RoadEdge* edge = *edge_ptr;
                    osm::WayNode* way_node = osm::way_find(city->osm_network, edge->way_id);
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

                    city::RoadInfo* chosen_edge = city->road.road_info_map->get(edge->id);
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
                osm::WayNode* way_node = osm::way_find(city->osm_network, hovered_object_id);
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

                    _tile_pipeline_add(batch, city, draw_frame->camera_resource_handle, neta_overlay_option);
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
                        _tile_pipeline_add(batch, city, draw_frame->camera_resource_handle, neta_overlay_option);
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
                        _tile_pipeline_add(batch, city, draw_frame->camera_resource_handle, neta_overlay_option);
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
    while (!streaming_ended)
    {
        render::gpu_work_update();
        streaming_ended = city_area_streaming_end(city);
    }
    // Task nodes and inputs may belong to road/network arenas; consume them first.
    _city_tasks_collect(city, false);
    Assert(!city->task_list.first);
    if (city->road.arena)
    {
        road_destroy(&city->road);
    }
    if (city->osm_network)
    {
        osm::osm_release(city->osm_network);
    }

    delete city->agent_system;
    city->agent_system = {};

    if (city->neta_state && city->neta_state->arena)
    {
        arena_release(city->neta_state->arena);
    }
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

g_internal async::AsyncTaskContinuation<RoadBuildTask>
road_build(async::ThreadInfo info, async::AsyncTaskStatus<RoadBuildTask>* status)
{
    (void)info;
    ScratchScope scratch = ScratchScope(0, 0);

    RoadBuildTask* task = status->user_data;
    Road* road = task->road;
    osm::Network* network = task->network;
    RoadBvh* bvh = task->bvh;

    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer({ render::thread_cmd_buffer_end(thread_ctx); });

    Map<S64, neta::EdgeList>* neta_edge_map =
        neta::osm_way_to_edges_map_create(road->arena, network, road->netascore_file_path, road->bbox);

    road->road_info_map =
        city::road_info_from_edge_id(road->arena, network, network->edge_structure.edges, neta_edge_map);

    Buffer<U8> colormap_buffer = Buffer<U8>((U8*)g_colormap_inferno, sizeof(g_colormap_inferno));
    render::BufferInfo colormap_buffer_info = render::BufferInfo(colormap_buffer, render::BufferType_StorageBuffer);
    road->colormap_handle = render::buffer_load_sync(thread_ctx, &colormap_buffer_info, S("colormap_buffer"));
    ////////////////////////////////////////
    // build road buffers
    RoadBuildResult result =
        city::road_segment_build(scratch.arena, network, network->edge_structure.edges, road->default_road_width,
                                 road->road_height, road->ecef_to_local, road->road_info_map);
    road->vertex_buffer_handle = result.vertex_buffer_handle;
    road->index_buffer_handle = result.index_buffer_handle;

    bvh_create(bvh, result.road_segment_buffer, 10);
    render::BufferInfo road_segment_buffer_info =
        render::BufferInfo(bvh->road_segment_buffer_sorted, render::BufferType_StorageBuffer);
    road->segment_buffer_handle =
        render::buffer_load_sync(thread_ctx, &road_segment_buffer_info, S("road_segment_buffer"));
    render::BufferInfo road_segment_node_buffer_info =
        render::BufferInfo(bvh->node_buffer, render::BufferType_StorageBuffer);
    road->segment_node_buffer_handle =
        render::buffer_load_sync(thread_ctx, &road_segment_node_buffer_info, S("road_segment_node_buffer"));
    //// build building buffers
    // render::SamplerInfo sampler_info = {
    //     .min_filter = render::Filter_Linear,
    //     .mag_filter = render::Filter_Linear,
    //     .mip_map_mode = render::MipMapMode_Linear,
    //     .address_mode_u = render::SamplerAddressMode_Repeat,
    //     .address_mode_v = render::SamplerAddressMode_Repeat,
    // };
    // city::buildings_build();
    // buildings_build(ctx->buildings, &sampler_info, ecef_to_local, ctx->road->road_height);
    return {};
}

g_internal async::AsyncTaskContinuation<AgentSimThread>
agent_sim_build(async::ThreadInfo info, async::AsyncTaskStatus<AgentSimThread>* status)
{
    (void)info;
    city::agents_create(status->user_data);
    return {};
}
g_internal void
road_destroy(Road* road)
{
    Context* ctx = dt_ctx_get();

    RoadBvh* bvh = 0;
    if (ctx->tile_load_state->polygon_bvh_pool->item_from_handle(road->bvh_handle, &bvh))
    {
        bvh->deletion_requested = true;
        if (bvh->loads_in_flight == 0)
        {
            arena_release(bvh->arena);
            ctx->tile_load_state->polygon_bvh_pool->item_free(road->bvh_handle);
        }
    }
    render::handle_destroy(road->vertex_buffer_handle);
    render::handle_destroy(road->index_buffer_handle);
    render::handle_destroy(road->texture_handle);
    render::handle_destroy(road->colormap_handle);
    render::handle_destroy_deferred(road->segment_buffer_handle);
    render::handle_destroy_deferred(road->segment_node_buffer_handle);

    road->road_info_map = {};
    arena_release(road->arena);
}

g_internal void
road_segment_from_road_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1,
                             F32 road_width)
{
    Vec2F64 road_0_pos = node_0.pos.xy;
    Vec2F64 road_1_pos = node_1.pos.xy;
    Vec2F64 road_dir = sub_2f64(road_1_pos, road_0_pos);
    Vec2F64 orthogonal_vec = vec_2f64(road_dir.y, -road_dir.x);
    Vec2F64 normal_scaled = vec_2f64(0.001f, 0.001f);
    if (orthogonal_vec.x != 0 && orthogonal_vec.y != 0)
    {
        Vec2F64 normal = normalize_2f64(orthogonal_vec);
        normal_scaled = scale_2f64(normal, road_width / 2.0f);
    }

    out_road_segment->start.top = add_2f64(road_0_pos, normal_scaled);
    out_road_segment->start.btm = sub_2f64(road_0_pos, normal_scaled);
    out_road_segment->end.top = add_2f64(road_1_pos, normal_scaled);
    out_road_segment->end.btm = sub_2f64(road_1_pos, normal_scaled);

    out_road_segment->start.node = node_0;
    out_road_segment->end.node = node_1;
}

// BVH functions /////////////////////////////

g_internal void
quick_select(Buffer<BoundingBox> center_buffer, U32 split_axis, U32 start_idx, U32 end_idx, U32 k)
{
    Assert(k < center_buffer.size);

    while (start_idx < end_idx)
    {
        U32 pivot_idx = end_idx - 1;
        F32 pivot = center_buffer.data[pivot_idx].center.v[split_axis];

        U32 divider_idx = start_idx;
        for (U32 i = start_idx; i < pivot_idx; ++i)
        {
            if (center_buffer.data[i].center.v[split_axis] < pivot)
            {
                Swap(BoundingBox, center_buffer.data[i], center_buffer.data[divider_idx]);
                divider_idx++;
            }
        }

        Swap(BoundingBox, center_buffer.data[divider_idx], center_buffer.data[pivot_idx]);

        if (divider_idx == k)
        {
            break;
        }
        else if (divider_idx < k)
        {
            start_idx = divider_idx + 1;
        }
        else
        {
            end_idx = divider_idx;
        }
    }
}

g_internal Rng2F32
bounds_union(Rng2F32 a, Rng2F32 b)
{
    Rng2F32 result;
    result.min.v[0] = Min(a.min.v[0], b.min.v[0]);
    result.min.v[1] = Min(a.min.v[1], b.min.v[1]);

    result.max.v[0] = Max(a.max.v[0], b.max.v[0]);
    result.max.v[1] = Max(a.max.v[1], b.max.v[1]);
    return result;
}

g_internal Rng2F32
bounds_union(Rng2F32 rng, Vec2F32 vec)
{
    Rng2F32 result;
    result.min.v[0] = Min(rng.min.v[0], vec.v[0]);
    result.min.v[1] = Min(rng.min.v[1], vec.v[1]);

    result.max.v[0] = Max(rng.max.v[0], vec.v[0]);
    result.max.v[1] = Max(rng.max.v[1], vec.v[1]);
    return result;
}

g_internal Rng2F32
bounds_union(Buffer<BoundingBox> center_buffer, U32 start_idx, U32 end_idx)
{
    Rng2F32 bounds = rng2f32_inverted_inf();
    for (U32 i = start_idx; i < end_idx; ++i)
    {
        BoundingBox seg_center = center_buffer.data[i];
        bounds = bounds_union(bounds, seg_center.bounds);
    }
    return bounds;
}

g_internal Axis2
split_axis_find(Buffer<BoundingBox> bb_buffer, U32 start_idx, U32 end_idx)
{
    Rng2F32 bounds = rng2f32_inverted_inf();
    for (U32 i = start_idx; i < end_idx; ++i)
    {
        BoundingBox* seg_center = &bb_buffer[i];
        for (U32 ax_idx = 0; ax_idx < Axis2_COUNT; ++ax_idx)
        {
            if (seg_center->center.v[ax_idx] < bounds.min.v[ax_idx])
            {
                bounds.min.v[ax_idx] = seg_center->center.v[ax_idx];
            }

            if (seg_center->center.v[ax_idx] > bounds.max.v[ax_idx])
            {
                bounds.max.v[ax_idx] = seg_center->center.v[ax_idx];
            }
        }
    }

    Vec2F32 diff = sub_2f32(bounds.max, bounds.min);
    Axis2 split_axis = diff.x > diff.y ? Axis2_X : Axis2_Y;
    return split_axis;
}

g_internal void
bvh_create(RoadBvh* bvh, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Buffer<BoundingBox> bb_buffer = buffer_alloc<BoundingBox>(scratch.arena, road_segment_buffer.size);

    // 1. for every element in the buffer, find the center point (used for segmentation)
    for (U32 i = 0; i < bb_buffer.size; i++)
    {
        // init idx to later point to RoadSegmentCorners*
        bb_buffer.data[i].idx = i;

        // create bounding box around road segment
        Rng2F32 bounds = rng2f32_inverted_inf();
        for (U32 j = 0; j < Corner_COUNT; ++j)
        {
            RoadSegmentCorners* seg = &road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            bounds = bounds_union(bounds, corner);
        }
        bb_buffer.data[i].bounds = bounds;

        // find center point
        Vec2F32 center = {};
        for (U32 j = 0; j < Corner_COUNT; ++j)
        {
            RoadSegmentCorners* seg = &road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            center = add_2f32(center, corner);
        }
        bb_buffer.data[i].center = scale_2f32(center, 1.0f / (F32)Corner_COUNT);
    }

    RoadBvhContext* bvh_ctx = PushStruct(scratch.arena, RoadBvhContext);
    bvh_ctx->road_segment_buffer = road_segment_buffer;
    bvh_ctx->bb_buffer = bb_buffer;
    bvh_ctx->leaf_bb_max = leaf_bb_max;

    RoadSegmentNode* root = PushStruct(bvh->arena, RoadSegmentNode);
    bvh_ctx->root = root;
    root->bounds = bounds_union(bb_buffer, 0, bb_buffer.size);
    root->start_idx = 0;
    root->end_idx = bb_buffer.size;

    SLLStackPush(bvh_ctx->stack, root);

    Buffer<RoadSegmentCorners> road_segment_buffer_sorted =
        buffer_alloc<RoadSegmentCorners>(bvh->arena, bvh_ctx->road_segment_buffer.size);
    while (bvh_ctx->stack)
    {
        bvh_ctx->road_segment_node_count += 1;
        RoadSegmentNode* node = bvh_ctx->stack;
        SLLStackPop(bvh_ctx->stack);

        U32 num_elem = node->end_idx - node->start_idx;

        if (num_elem <= leaf_bb_max)
        {
            for (U32 i = node->start_idx; i < node->end_idx; ++i)
            {
                // copy road segment corners from bb_buffer index to sorted buffer
                BoundingBox bb = bvh_ctx->bb_buffer.data[i];
                road_segment_buffer_sorted.data[i] = bvh_ctx->road_segment_buffer.data[bb.idx];
            }
            continue;
        }

        Axis2 split_axis = split_axis_find(bvh_ctx->bb_buffer, node->start_idx, node->end_idx);
        U32 split_idx = node->start_idx + (num_elem) / 2;
        quick_select(bb_buffer, (U32)split_axis, node->start_idx, node->end_idx, split_idx);
        node->split_axis = split_axis;
        node->split_value = bb_buffer.data[split_idx].center.v[(U32)split_axis];

        Rng2F32 idx_range = rng_2f32(V2F32(node->start_idx, split_idx), V2F32(split_idx, node->end_idx));

        for (U32 i = 0; i < ArrayCount(idx_range.v); ++i)
        {
            node->children[i] = PushStruct(bvh->arena, RoadSegmentNode);
            node->children[i]->start_idx = idx_range.min.v[i];
            node->children[i]->end_idx = idx_range.max.v[i];
            node->children[i]->bounds =
                bounds_union(bb_buffer, node->children[i]->start_idx, node->children[i]->end_idx);
        }

        SLLStackPush(bvh_ctx->stack, node->children[1]);
        SLLStackPush(bvh_ctx->stack, node->children[0]);
    }

    // create the final road segment node for storage buffer usage
    Buffer<RoadSegmentNodeStorageBuffer> road_segment_node_buffer =
        buffer_alloc<RoadSegmentNodeStorageBuffer>(bvh->arena, bvh_ctx->road_segment_node_count);
    Assert(bvh_ctx->stack == 0);
    SLLStackPush(bvh_ctx->stack, bvh_ctx->root);
    U32 cur_node_idx = 0;
    while (bvh_ctx->stack)
    {
        RoadSegmentNode* node = bvh_ctx->stack;
        SLLStackPop(bvh_ctx->stack);

        node->final_idx = cur_node_idx++;
        RoadSegmentNodeStorageBuffer* current = &road_segment_node_buffer[node->final_idx];
        current->min_x = node->bounds.min.x;
        current->min_y = node->bounds.min.y;
        current->max_x = node->bounds.max.x;
        current->max_y = node->bounds.max.y;
        current->split_axis = node->split_axis;
        current->split_value = node->split_value;

        // fill out parent nodes second child idx
        if (node->parent)
        {
            U32 parent_idx = node->parent->final_idx;
            RoadSegmentNodeStorageBuffer* parent = &road_segment_node_buffer[parent_idx];
            parent->child_1_idx = node->final_idx;
        }

        if (node->children[0] && node->children[1])
        {
            current->is_leaf = false;
            current->child_0_idx = cur_node_idx;

            SLLStackPush(bvh_ctx->stack, node->children[1]);
            SLLStackPush(bvh_ctx->stack, node->children[0]);
            node->children[1]->parent = node;
        }
        else
        {
            current->is_leaf = true;
            current->start_idx = node->start_idx;
            current->end_idx = node->end_idx;
        }
    }
    Assert(cur_node_idx == road_segment_node_buffer.size);

    bvh->root = bvh_ctx->root;
    bvh->road_segment_buffer_sorted = road_segment_buffer_sorted;
    bvh->node_buffer = road_segment_node_buffer;
}

g_internal void
assert_non_intersecting_edges(glm::vec2* vertices, U32 count)
{
    AssertAlways(count >= 4);
    glm::vec2 v0 = vertices[0];
    glm::vec2 v1 = vertices[1];
    glm::vec2 v2 = vertices[2];
    glm::vec2 v3 = vertices[3];

    glm::vec2 _dummy;
    AssertAlways(geometry::line_intersection(v0, v1, v2, v3, &_dummy) == false);
    AssertAlways(geometry::line_intersection(v1, v2, v3, v0, &_dummy) == false);
}

g_internal city::RoadBuildResult
road_segment_build(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> edge_buffer, F32 default_road_width,
                   F32 road_height, glm::dmat4& ecef_to_local, Map<osm::EdgeId, RoadInfo>* road_info_map)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(&arena, 1);

    Buffer<render::Vertex3DBlend> vertex_buffer = buffer_alloc<render::Vertex3DBlend>(arena, edge_buffer.size * 4);
    Buffer<U32> index_buffer = buffer_alloc<U32>(arena, edge_buffer.size * 6);
    Buffer<RoadSegmentCorners> corner_buffer = buffer_alloc<RoadSegmentCorners>(arena, edge_buffer.size);

    U32 cur_vertex_idx = 0;
    U32 cur_index_idx = 0;
    for (U32 i = 0; i < edge_buffer.size; i++)
    {
        osm::RoadEdge* edge = &edge_buffer[i];

        osm::EcefLocation start_node = osm::location_get(network, edge->node_id_from);
        osm::EcefLocation end_node = osm::location_get(network, edge->node_id_to);

        RoadSegment road_segment;
        road_segment_from_road_nodes(&road_segment, start_node, end_node, default_road_width);

        // Road coordinates stored in buffer for 3D geometry projection
        RoadSegmentCorners* road_segment_corners = &corner_buffer[i];
        road_segment_corners->edge_id = edge->id;
        glm::vec2 local_top_left =
            glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.top.x, road_segment.start.top.y,
                                                 road_segment.start.node.pos.z, 1.0));
        glm::vec2 local_top_right = glm::vec2(ecef_to_local * glm::dvec4(road_segment.end.top.x, road_segment.end.top.y,
                                                                         road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_right =
            glm::vec2(ecef_to_local *
                      glm::dvec4(road_segment.end.btm.x, road_segment.end.btm.y, road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_left =
            glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.btm.x, road_segment.start.btm.y,
                                                 road_segment.start.node.pos.z, 1.0));
        road_segment_corners->corners[RoadSegmentCornerCoord_TopLeft] = vec_2f32(local_top_left.x, local_top_left.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_TopRight] = vec_2f32(local_top_right.x, local_top_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomRight] =
            vec_2f32(local_bottom_right.x, local_bottom_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomLeft] =
            vec_2f32(local_bottom_left.x, local_bottom_left.y);

        // Assert non_intersecting edges
        {
            glm::vec2 vertices[RoadSegmentCornerCoord_Count] = {local_top_left, local_top_right, local_bottom_right,
                                                                local_bottom_left};
            assert_non_intersecting_edges(vertices, RoadSegmentCornerCoord_Count);
        }
        RoadInfo* road_info = road_info_map->get(edge->id);
        if (road_info)
        {
            road_segment_corners->road_info = *road_info;
        }

        // add vertex and inde
        quad_to_buffer_add(road_segment_corners, vertex_buffer, index_buffer, edge->id, road_height, &cur_vertex_idx,
                           &cur_index_idx);
    }

    render::BufferInfo vertex_buffer_info = render::BufferInfo(vertex_buffer, render::BufferType_Vertex);
    render::BufferInfo index_buffer_info = render::BufferInfo(index_buffer, render::BufferType_Index);

    render::Handle vertex_buffer_handle = render::buffer_load_async(&vertex_buffer_info);
    render::Handle index_buffer_handle = render::buffer_load_async(&index_buffer_info);

    city::RoadBuildResult road_build_result = {.vertex_buffer_handle = vertex_buffer_handle,
                                               .index_buffer_handle = index_buffer_handle,
                                               .road_segment_buffer = corner_buffer};
    return road_build_result;
}

g_internal Vec3F64
height_dim_add(Vec2F64 pos, F64 height)
{
    Vec3F64 result = vec_3f64(pos.x, pos.y, height);
    return result;
}

g_internal void
quad_to_buffer_add(RoadSegmentCorners* road_segment, Buffer<render::Vertex3DBlend> buffer, Buffer<U32> indices,
                   U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx)
{
    F32 road_width = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_TopLeft],
                               road_segment->corners[RoadSegmentCornerCoord_BottomLeft]);
    F32 top_tex_scaled = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_TopLeft],
                                   road_segment->corners[RoadSegmentCornerCoord_TopRight]) /
                         road_width;
    F32 btm_tex_scaled = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_BottomLeft],
                                   road_segment->corners[RoadSegmentCornerCoord_BottomRight]) /
                         road_width;

    const F32 uv_x_top = 1;
    const F32 uv_x_btm = 0;
    const F32 uv_y_start = 0.5f - (F32)btm_tex_scaled;
    const F32 uv_y_end = 0.5f + (F32)top_tex_scaled;

    U32 base_vertex_idx = *cur_vertex_idx;
    U32 base_index_idx = *cur_index_idx;

    Vec2U32 id = {.u64 = edge_id};
    Vec2F32 blend_factor = vec_2f32(0.0f, 0.0f);

    // quad of vertices
    buffer.data[base_vertex_idx] = {
        .pos = vec3f32_from_64(
            height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_TopLeft]), road_height)),
        .uv = vec_2f32(uv_x_top, uv_y_start),
        .object_id = id,
        .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 1] = {
        .pos = vec3f32_from_64(
            height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_BottomLeft]), road_height)),
        .uv = vec_2f32(uv_x_btm, uv_y_start),
        .object_id = id,
        .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 2] = {
        .pos = vec3f32_from_64(
            height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_TopRight]), road_height)),
        .uv = vec_2f32(uv_x_top, uv_y_end),
        .object_id = id,
        .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 3] = {
        .pos = vec3f32_from_64(
            height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_BottomRight]), road_height)),
        .uv = vec_2f32(uv_x_btm, uv_y_end),
        .object_id = id,
        .blend_factor = blend_factor};

    // creating quad from
    indices.data[base_index_idx] = base_vertex_idx;
    indices.data[base_index_idx + 1] = base_vertex_idx + 1;
    indices.data[base_index_idx + 2] = base_vertex_idx + 2;
    indices.data[base_index_idx + 3] = base_vertex_idx + 1;
    indices.data[base_index_idx + 4] = base_vertex_idx + 2;
    indices.data[base_index_idx + 5] = base_vertex_idx + 3;

    *cur_vertex_idx += 4;
    *cur_index_idx += 6;
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
road_create(City* city, Road* in_out_road, glm::dmat4& ecef_to_local, String8 area, String8 bbox_cache_str)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Context* ctx = dt_ctx_get();

    in_out_road->arena = arena_alloc();
    Debug_SetName(in_out_road->arena, "city road network arena");
    in_out_road->ecef_to_local = ecef_to_local;
    in_out_road->road_height = 10.0f;
    in_out_road->default_road_width = 2.0f;
    in_out_road->bvh_handle = ctx->tile_load_state->polygon_bvh_pool->handle_get();
    RoadBvh* bvh = 0;
    Assert(ctx->tile_load_state->polygon_bvh_pool->item_from_handle(in_out_road->bvh_handle, &bvh));
    bvh->arena = arena_alloc();

    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_Repeat,
        .address_mode_v = render::SamplerAddressMode_Repeat,
    };
    String8 texture_path = str8_path_from_str8_list(
        scratch.arena, {ctx->data_subdirs.data[dt_DataDirType::Texture], S("road_texture.ktx2")});
    in_out_road->texture_handle = render::texture_load_async(&sampler_info, texture_path);

    U64 node_hashmap_size = 1000;
    U64 way_hashmap_size = 100;
    city->osm_network = osm::osm_init(node_hashmap_size, way_hashmap_size,
                                      ctx->data_subdirs.data[dt_DataDirType::Cache], area, bbox_cache_str);
}

g_internal Map<osm::EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> road_edge_buf,
                       Map<S64, neta::EdgeList>* neta_edge_map)
{
    prof_scope_marker;
    Map<osm::EdgeId, RoadInfo>* road_info_map = Map<osm::EdgeId, RoadInfo>::create(arena, 1024);

    for (osm::RoadEdge& edge : road_edge_buf)
    {
        neta::Edge* neta_edge = edge_from_road_edge(network, &edge, neta_edge_map);
        if (neta_edge)
        {
            RoadInfo info = {};
            info.options[RoadOverlayOption_Bikeability_ft] = neta_edge->index_bike_ft;
            info.options[RoadOverlayOption_Bikeability_tf] = neta_edge->index_bike_tf;
            info.options[RoadOverlayOption_Walkability_ft] = neta_edge->index_walk_ft;
            info.options[RoadOverlayOption_Walkability_tf] = neta_edge->index_walk_tf;
            road_info_map->insert(arena, edge.id, info);
        }
    }

    return road_info_map;
}

g_internal neta::Edge*
edge_from_road_edge(osm::Network* network, osm::RoadEdge* road_edge, Map<osm::WayId, neta::EdgeList>* edge_list_map)
{
    S64 from_id = road_edge->node_id_from;
    S64 to_id = road_edge->node_id_to;

    osm::WgsLocation from_node_loc = osm::wgs_location_get(network, from_id);
    osm::WgsLocation to_node_loc = osm::wgs_location_get(network, to_id);
    Vec2F64 from_node_coord = vec_2f64(from_node_loc.lon, from_node_loc.lat);
    Vec2F64 to_node_coord = vec_2f64(to_node_loc.lon, to_node_loc.lat);

    S64 way_id = road_edge->way_id;
    neta::EdgeList* edge_list = edge_list_map->get(way_id);

    neta::Edge* chosen_edge = {};
    if (edge_list)
    {
        F64 smallest_dist = max_f64;
        for (neta::EdgeNode* edge_node = edge_list->first; edge_node; edge_node = edge_node->next)
        {
            neta::Edge* edge = edge_node->edge;
            for (Vec2F64& coord : edge->coords)
            {
                F64 from_dist = dist_2f64(coord, from_node_coord);
                F64 to_dist = dist_2f64(coord, to_node_coord);
                F64 closest_dist = Min(from_dist, to_dist);
                if (closest_dist < smallest_dist)
                {
                    smallest_dist = closest_dist;
                    chosen_edge = edge;
                }
            }
        }
    }
    return chosen_edge;
}

g_internal void
_city_tasks_collect(City* city, bool publish_agents)
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
                }
            }
            break;
            case AsyncTaskType::Road:
            {
                city->road_building_done = false;
                if (auto completed = async::async_task_take_completed(task->road))
                {
                    task_done = true;
                    city->road_building_done = !completed->error;
                }
            }
            break;
            case AsyncTaskType::CarSim:
            {
                if (publish_agents) city->cars_creation_done = false;
                if (auto completed = async::async_task_take_completed(task->agent_sim_task))
                {
                    task_done = true;
                    if (publish_agents) city->cars_creation_done = !completed->error;
                    AgentSimThread* agent_sim_thread = completed->user_data;
                    if (!completed->error && publish_agents)
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
            case city::AsyncTaskType::Cached:
            {
                switch (task->cached_type)
                {
                    case AsyncTaskType::Neta:
                    {
                        task_done = true;
                        city->neta_task_done = true;
                        INFO_LOG("Neta cached state loaded");
                    };
                    break;
                    default: InvalidPath; break;
                }
            };
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
