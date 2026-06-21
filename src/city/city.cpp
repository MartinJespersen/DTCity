namespace city
{

g_internal void
city_init(City* city, String8 cache_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = arena_alloc();
    city->cache_path = push_str8_copy(arena, cache_path);
    city->arena = arena;
    city->agent_scale_factor = 1.0f;
}

g_internal void
city_build(City* city, const CityInfo* city_config, Rng2F64 bbox, String8 tileset_url, String8 area)
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
    cesium::tileset_renderer_create(city->arena, &city->cesium, ctx->thread_pool, tileset_url, bbox_center.x, bbox_center.y, 0.0, city_config->custom_geometry_enabled);
    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(bbox_center.x), glm::radians(bbox_center.y), 0);
    CesiumGeospatial::LocalHorizontalCoordinateSystem* local_coord = new CesiumGeospatial::LocalHorizontalCoordinateSystem(
        origin_cartographic, CesiumGeospatial::LocalDirection::East, CesiumGeospatial::LocalDirection::North, CesiumGeospatial::LocalDirection::Up);
    glm::dmat4 ecef_to_local = local_coord->getEcefToLocalTransformation();

    // road init
    Road* road = &city->road;
    city::road_create(city, road, ecef_to_local, area, bbox_cache_str);
    road->bbox = bbox;

    // init neta layer
    Arena* arena = arena_alloc();
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
        String8 str = PushStr8F(scratch.arena, "way[\"%s\"](%f, %f, %f, %f);\n", osm::g_waytype_osm_tag[way_type_idx], road->bbox.min.y, road->bbox.min.x, road->bbox.max.y, road->bbox.max.x);
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
    Result<String8> cache_result = cache_read(osm_network->arena, osm_network->cache_file_location, osm_network->bbox_cache_str);

    AsyncCityTask* osm_task = PushStruct(road->arena, AsyncCityTask);
    osm_task->type = city::AsyncTaskType::Osm;
    if (cache_result.err)
    {
        async::HttpInfo* http_info =
            async::http_info_create(osm_network->arena, HTTP_Method_Post, path, S("application/x-www-form-urlencoded"), {S("User-Agent: DTCity/0.1"), S("Accept: application/json")}, {});
        http_info->body = push_str8_copy(osm_network->arena, body);
        async::AsyncHttpTaskStateConfig<osm::Network> config = async::AsyncHttpTaskStateConfig<osm::Network>(osm::fetch_osm_data_and_parse, osm_network, 3, 1);
        async::AsyncHttpTaskCreateResult<osm::Network> http_task_result = async::async_http_task_run(thread_pool, http_info, &config, S("Osm Task"));
        AssertAlways(http_task_result.async_result.has_error() == false);

        osm_task->osm = http_task_result.task_state;
    }
    else
    {
        async::AsyncTaskStatus<osm::Network>* osm_task_cached = async::async_task_run(thread_pool, osm::parse_osm_data, osm_network, S("Osm Task Http Cached"));
        osm_task->osm = osm_task_cached;
    }

    // parse nodes
    // ~mgj: parse OSM way structures
    return osm_task;
}

g_internal void
city_update(City* city, Buffer<city::Coordinate> new_agent_coords, async::ThreadPool* thread_pool, RoadOverlayOption neta_overlay_option, Vec2U32 framebuffer_dim, const CityInfo* city_config)
{
    Context* ctx = dt_ctx_get();
    ui::Camera* camera = resource_pool_item_from_idx(ctx->camera_container, city->camera_handle);
    // TODO: vulkan current frame should not be used directly
    U32 current_frame = vulkan::ctx_get()->current_frame;
    render::MappedHandle<ui::CameraUniformBuffer> camera_handle = camera->mut_handles[current_frame];

    for (AsyncCityTask* task = city->task_list.first; task;)
    {
        AsyncCityTask* next_task = task->next;
        bool task_done = false;
        switch (task->type)
        {
            case AsyncTaskType::Osm:
            {
                async::AsyncTaskResult<osm::Network> task_result = async::async_task_is_done(task->osm);
                task_done = task_result.done;
                city->osm_task_done = task_result.success;
            };
            break;
            case AsyncTaskType::Neta:
            {
                async::AsyncTaskResult<neta::NetaTaskState> task_result = async::async_task_is_done(task->neta);
                task_done = task_result.done;
                city->neta_task_done = task_result.success;
            }
            break;
            case AsyncTaskType::Road:
            {
                async::AsyncTaskResult<RoadBuildTask> task_result = async::async_task_is_done(task->road);
                task_done = task_result.done;
                city->road_building_done = task_result.success;
            }
            break;
            case AsyncTaskType::CarSim:
            {
                async::AsyncTaskResult<CarSimBuildTask> task_result = async::async_task_is_done(task->car_sim);
                task_done = task_result.done;
                city->cars_creation_done = task_result.success;
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

    U64 hovered_object_id = render::latest_hovered_object_id_get();

    if (city->neta_task_done && city->osm_task_done && !city->road_building_started)
    {
        city->road_building_started = true;
        DEBUG_LOG("City building work has been schedule");
        RoadBuildTask* road_build_task = PushStruct(city->arena, RoadBuildTask);
        road_build_task->road = &city->road;
        road_build_task->network = city->osm_network;
        async::AsyncTaskStatus<RoadBuildTask>* road_building_task = async::async_task_run(thread_pool, road_build, road_build_task, S("Road Building Task"));
        AsyncCityTask* road_task_list_elem = PushStruct(city->arena, AsyncCityTask);
        road_task_list_elem->type = AsyncTaskType::Road;
        road_task_list_elem->road = road_building_task;
        DLLPushBack(city->task_list.first, city->task_list.last, road_task_list_elem);
    }

    if (city->osm_task_done)
    {
        osm::RoadEdge** edge_ptr = map_get(&city->osm_network->edge_structure.edge_map, (S64)hovered_object_id);
        if (edge_ptr)
        {
            osm::RoadEdge* edge = *edge_ptr;
            osm::WayNode* way_node = osm::way_find(city->osm_network, edge->way_id);
            osm::Way* way = &way_node->way;

            bool open = true;
            ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
            for (osm::Tag& tag : way->tags)
            {
                ImGui::Text("%s: %s", (char*)tag.key.str, (char*)tag.value.str);
            }

            city::RoadInfo* chosen_edge = map_get(city->road.road_info_map, edge->id);
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
    }
    if (city->osm_task_done)
    {
        osm::WayNode* way_node = osm::way_find(city->osm_network, hovered_object_id);
        if (way_node)
        {
            osm::Way* way = &way_node->way;
            bool open = true;
            ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
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
    }

    // input:
    // - Bounding box (in 3D to include height)
    //  - Using local coordinates from cesium library?
    // - texture handle of texture to use
    //

    // Update and render Cesium 3D Tiles ////////////
    cesium::TilesetRenderer* renderer = &city->cesium;
    cesium::tileset_update_view(renderer, camera, framebuffer_dim, ctx->time->delta_time_sec);

    bool overlay_option_changed = neta_overlay_option != city->road.overlay_option_cur;
    if (city->road_building_done)
    {
        city->road.overlay_option_cur = neta_overlay_option;
    }
    for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
    {
        if (city->road_building_done)
        {
            if (tile->compute_scheduled == false || overlay_option_changed)
            {
                tile->compute_scheduled = draw::draw_road_intersection_compute(tile->render_data.vertex_buffer_handle, tile->render_data.index_buffer_handle, city->road.segment_buffer_handle,
                                                                               city->road.segment_node_buffer_handle, neta_overlay_option);
            }
        }
        render::Handle colormap_handle = city->road.overlay_option_cur ? city->road.colormap_handle : city->road.zero_colormap_handle;

        if (tile->render_data.is_map_tile && city_config->bbox_clipping_enabled)
        {
            F32 border_offset_lon_m = 50;
            F32 border_offset_lat_m = 50;
            CesiumGeospatial::Cartographic bbox_min_cartographic(glm::radians(city->bbox.min.x), glm::radians(city->bbox.min.y), 0);
            CesiumGeospatial::Cartographic bbox_max_cartographic(glm::radians(city->bbox.max.x), glm::radians(city->bbox.max.y), 0);

            glm::dvec3 bbox_min_ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(bbox_min_cartographic);
            glm::dvec3 bbox_max_ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(bbox_max_cartographic);

            glm::dvec4 bbox_min_local = city->cesium.ecef_to_local * glm::dvec4(bbox_min_ecef, 1.0);
            glm::dvec4 bbox_max_local = city->cesium.ecef_to_local * glm::dvec4(bbox_max_ecef, 1.0);

            border_offset_lon_m = Min((bbox_max_local.x - bbox_min_local.x) * 0.5, border_offset_lon_m);
            border_offset_lat_m = Min((bbox_max_local.y - bbox_min_local.y) * 0.5, border_offset_lat_m);
            tile->render_data.bbox_min = {.x = (F32)bbox_min_local.x + border_offset_lon_m, .y = (F32)bbox_min_local.y + border_offset_lat_m};
            tile->render_data.bbox_max = {.x = (F32)bbox_max_local.x - border_offset_lon_m, .y = (F32)bbox_max_local.y - border_offset_lat_m};
            tile->render_data.depth_bias = 100;
            tile->render_data.height_offset = -renderer->height_offset;
        }
        tile->render_data.colormap_handle = colormap_handle;
        tile->render_data.camera_handle = render::mapped_handle_erased(camera_handle);
        render::model_3d_bucket_add(&tile->render_data);
    }

    if (city->cars_creation_started == false && city->osm_task_done)
    {
        Allocator* allocator = Allocator::create();
        AgentSim* car_sim = &city->car_sim;
        car_sim->allocator = allocator;
        car_sim->asset_dir = push_str8_copy(allocator->arena, ctx->data_subdirs.data[dt_DataDirType::Assets]);
        car_sim->texture_dir = push_str8_copy(allocator->arena, ctx->data_subdirs.data[dt_DataDirType::Texture]);
        car_sim->agent_count = 100;
        car_sim->max_agent_count = 10000;
        CarSimBuildTask* car_sim_build_task = PushStruct(allocator->arena, CarSimBuildTask);
        car_sim_build_task->car_sim = car_sim;
        car_sim_build_task->network = city->osm_network;
        async::AsyncTaskStatus<CarSimBuildTask>* car_sim_task = async::async_task_run(ctx->thread_pool, agent_sim_build, car_sim_build_task, S("Car Sim Task"));

        AsyncCityTask* car_sim_task_list_elem = PushStruct(allocator->arena, AsyncCityTask);
        car_sim_task_list_elem->type = AsyncTaskType::CarSim;
        car_sim_task_list_elem->car_sim = car_sim_task;
        DLLPushBack(city->task_list.first, city->task_list.last, car_sim_task_list_elem);

        city->cars_creation_started = true;
    }

    /// car simulation rendering
    if (city->cars_creation_done)
    {
        AgentSim* agent_sim = &city->car_sim;
        F32 scale_factor = city->agent_scale_factor;
        agent_sim_update(&city->car_sim, new_agent_coords, city->cesium.ecef_to_local, scale_factor);

        Buffer<render::Model3DInstance> instance_buffer = buffer_alloc<render::Model3DInstance>(draw::draw_frame_arena_get(), agent_sim->agents_active->size);
        for (U64 agent_idx = 0; agent_idx < agent_sim->agents_active->size; agent_idx += 1)
        {
            instance_buffer.data[agent_idx] = (*agent_sim->agents_active)[agent_idx].model_matrix;
        }

        // instance buffer offset alignment and assignment
        render::BufferInfo instance_buffer_info = render::BufferInfo(instance_buffer, render::BufferType_Vertex | render::BufferType_StorageBuffer);
        render::MappedHandle<void> camera_handle_void = render::mapped_handle_erased(camera_handle);
        draw::CarInstanceDrawResult draw_result = draw::draw_car_instance_render(camera_handle_void, city->car_sim.meshes, city->car_sim.texture_handles, &instance_buffer_info);

        if (draw_result.render_scheduled)
        {
            U32 instance_buffer_offset = draw_result.buffer_offset;
            for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
            {
                bool map_tile_reference = tile->render_data.is_map_tile && (city_config->custom_geometry_enabled == false);
                bool custom_geometry_reference = (tile->render_data.is_map_tile == false) && city_config->custom_geometry_enabled;

                if (map_tile_reference || custom_geometry_reference)
                {
                    render::agent_instance_compute_bucket_add(&instance_buffer_info, tile->render_data.vertex_buffer_handle, tile->render_data.index_buffer_handle,
                                                              -city->car_sim.agent_center_offset.min, instance_buffer_offset);
                }
            }
        }
    }
}

g_internal void
city_release(City* city)
{
    road_destroy(&city->road);
    osm::osm_release(city->osm_network);
    cesium::tileset_renderer_destroy(&city->cesium);
    agent_sim_destroy(&city->car_sim);

    arena_release(city->neta_state->arena);
    arena_release(city->arena);
}

g_internal async::AsyncTaskContinuation<RoadBuildTask>
road_build(async::ThreadInfo info, async::AsyncTaskStatus<RoadBuildTask>* status)
{
    (void)info;

    RoadBuildTask* task = status->user_data;
    Road* road = task->road;
    osm::Network* network = task->network;

    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer({ render::thread_cmd_buffer_end(thread_ctx); });

    Map<S64, neta::EdgeList>* neta_edge_map = neta::osm_way_to_edges_map_create(road->arena, network, road->netascore_file_path, road->bbox);

    road->road_info_map = city::road_info_from_edge_id(road->arena, network, network->edge_structure.edges, neta_edge_map);

    road->colormap_handle = render::colormap_load_sync(thread_ctx, &road->colormap_sampler, g_colormap_inferno, sizeof(g_colormap_inferno));
    ////////////////////////////////////////
    // build road buffers
    road->road_build_result = city::road_segment_build(road->arena, network, network->edge_structure.edges, road->default_road_width, road->road_height, road->ecef_to_local, road->road_info_map);
    render::BufferInfo road_segment_buffer_info = render::BufferInfo(road->road_build_result.bvh_result.road_segment_buffer_sorted, render::BufferType_StorageBuffer);
    road->segment_buffer_handle = render::buffer_load_sync(thread_ctx, &road_segment_buffer_info);
    render::BufferInfo road_segment_node_buffer_info = render::BufferInfo(road->road_build_result.bvh_result.node_buffer, render::BufferType_StorageBuffer);
    road->segment_node_buffer_handle = render::buffer_load_sync(thread_ctx, &road_segment_node_buffer_info);
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

g_internal async::AsyncTaskContinuation<CarSimBuildTask>
agent_sim_build(async::ThreadInfo info, async::AsyncTaskStatus<CarSimBuildTask>* status)
{
    (void)info;
    CarSimBuildTask* task = status->user_data;
    city::agents_create(task->car_sim, task->network);
    return {};
}
g_internal void
road_destroy(Road* road)
{
    render::handle_destroy(road->road_build_result.vertex_buffer_handle);
    render::handle_destroy(road->road_build_result.index_buffer_handle);
    render::handle_destroy(road->colormap_handle);
    render::handle_destroy(road->zero_colormap_handle);
    render::handle_destroy_deferred(road->segment_buffer_handle);
    render::handle_destroy_deferred(road->segment_node_buffer_handle);

    arena_release(road->arena);
}

g_internal void
road_segment_from_road_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1, F32 road_width)
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

// find the two nodes connecting two road segments
// source: https://opensage.github.io/blog/roads-how-boring-part-5-connecting-the-road-segments
g_internal void
road_segments_coalesce(RoadSegment* in_out_road_segment_0, RoadSegment* in_out_road_segment_1, F32 road_width)
{
    // find a single node connecting two road segments for both the top and bottom of the road
    // segments
    Vec2F64 road0_top = in_out_road_segment_0->end.top;
    Vec2F64 road1_top = in_out_road_segment_1->start.top;
    Vec2F64 shared_center = in_out_road_segment_0->end.node.pos.xy;

    Vec2F64 road0_top_dir_norm = normalize_2f64(sub_2f64(road0_top, shared_center));
    Vec2F64 road1_top_dir_norm = normalize_2f64(sub_2f64(road1_top, shared_center));
    // take average of the two directions and normalize it
    Vec2F64 shared_top_normal = normalize_2f64(div_2f64(add_2f64(road0_top_dir_norm, road1_top_dir_norm), 2.0f));
    F64 cos_angle = dot_2f64(shared_top_normal, road1_top_dir_norm);
    F64 shared_top_len = (road_width / 2.0f) / cos_angle;

    Vec2F64 shared_top_dir = scale_2f64(shared_top_normal, shared_top_len);
    Vec2F64 shared_top = add_2f64(shared_center, shared_top_dir);
    Vec2F64 shared_btm = sub_2f64(shared_center, shared_top_dir);

    in_out_road_segment_0->end.btm = shared_btm;
    in_out_road_segment_0->end.top = shared_top;

    in_out_road_segment_1->start.btm = shared_btm;
    in_out_road_segment_1->start.top = shared_top;
}

g_internal F32
tag_value_get(Arena* arena, String8 key, F32 default_width, Buffer<osm::Tag> tags)
{
    F32 road_width = default_width; // Example value, adjust as needed
    {
        osm::TagResult result = osm::tag_find(arena, tags, key);
        if (result.result == osm::TagResultEnum::ROAD_TAG_FOUND)
        {
            F32 float_result = {0};
            if (F32FromStr8(result.value, &float_result))
            {
                road_width = float_result;
            }
        }
    }
    return road_width;
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
        BoundingBox* seg_center = bb_buffer[i];
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

g_internal BvhResult
bvh_create(Arena* arena, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

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
            RoadSegmentCorners* seg = road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            bounds = bounds_union(bounds, corner);
        }
        bb_buffer.data[i].bounds = bounds;

        // find center point
        Vec2F32 center = {};
        for (U32 j = 0; j < Corner_COUNT; ++j)
        {
            RoadSegmentCorners* seg = road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            center = add_2f32(center, corner);
        }
        bb_buffer.data[i].center = scale_2f32(center, 1.0f / (F32)Corner_COUNT);
    }

    BvhContext* bvh = PushStruct(scratch.arena, BvhContext);
    bvh->road_segment_buffer = road_segment_buffer;
    bvh->bb_buffer = bb_buffer;
    bvh->leaf_bb_max = leaf_bb_max;

    RoadSegmentNode* root = PushStruct(scratch.arena, RoadSegmentNode);
    bvh->root = root;
    root->bounds = bounds_union(bb_buffer, 0, bb_buffer.size);
    root->start_idx = 0;
    root->end_idx = bb_buffer.size;

    SLLStackPush(bvh->stack, root);

    Buffer<RoadSegmentCorners> road_segment_buffer_sorted = buffer_alloc<RoadSegmentCorners>(arena, bvh->road_segment_buffer.size);
    while (bvh->stack)
    {
        bvh->road_segment_node_count += 1;
        RoadSegmentNode* node = bvh->stack;
        SLLStackPop(bvh->stack);

        U32 num_elem = node->end_idx - node->start_idx;

        if (num_elem <= leaf_bb_max)
        {
            for (U32 i = node->start_idx; i < node->end_idx; ++i)
            {
                // copy road segment corners from bb_buffer index to sorted buffer
                BoundingBox bb = bvh->bb_buffer.data[i];
                road_segment_buffer_sorted.data[i] = bvh->road_segment_buffer.data[bb.idx];
            }
            continue;
        }

        Axis2 split_axis = split_axis_find(bvh->bb_buffer, node->start_idx, node->end_idx);
        U32 split_idx = node->start_idx + (num_elem) / 2;
        quick_select(bb_buffer, (U32)split_axis, node->start_idx, node->end_idx, split_idx);
        node->split_axis = split_axis;
        node->split_value = bb_buffer.data[split_idx].center.v[(U32)split_axis];

        Rng2F32 idx_range = rng_2f32(V2F32(node->start_idx, split_idx), V2F32(split_idx, node->end_idx));

        for (U32 i = 0; i < ArrayCount(idx_range.v); ++i)
        {
            node->children[i] = PushStruct(scratch.arena, RoadSegmentNode);
            node->children[i]->start_idx = idx_range.min.v[i];
            node->children[i]->end_idx = idx_range.max.v[i];
            node->children[i]->bounds = bounds_union(bb_buffer, node->children[i]->start_idx, node->children[i]->end_idx);
        }

        SLLStackPush(bvh->stack, node->children[1]);
        SLLStackPush(bvh->stack, node->children[0]);
    }

    // create the final road segment node for storage buffer usage
    Buffer<RoadSegmentNodeStorageBuffer> road_segment_node_buffer = buffer_alloc<RoadSegmentNodeStorageBuffer>(arena, bvh->road_segment_node_count);
    Assert(bvh->stack == 0);
    SLLStackPush(bvh->stack, bvh->root);
    U32 cur_node_idx = 0;
    while (bvh->stack)
    {
        RoadSegmentNode* node = bvh->stack;
        SLLStackPop(bvh->stack);

        node->final_idx = cur_node_idx++;
        RoadSegmentNodeStorageBuffer* current = road_segment_node_buffer[node->final_idx];
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
            RoadSegmentNodeStorageBuffer* parent = road_segment_node_buffer[parent_idx];
            parent->child_1_idx = node->final_idx;
        }

        if (node->children[0] && node->children[1])
        {
            current->is_leaf = false;
            current->child_0_idx = cur_node_idx;

            SLLStackPush(bvh->stack, node->children[1]);
            SLLStackPush(bvh->stack, node->children[0]);
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

    BvhResult result = {road_segment_buffer_sorted, road_segment_node_buffer};
    return result;
}

g_internal city::RoadBuildResult
road_segment_build(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> edge_buffer, F32 default_road_width, F32 road_height, glm::dmat4& ecef_to_local,
                   Map<osm::EdgeId, RoadInfo>* road_info_map)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Buffer<render::Vertex3DBlend> vertex_buffer = buffer_alloc<render::Vertex3DBlend>(arena, edge_buffer.size * 4);
    Buffer<U32> index_buffer = buffer_alloc<U32>(arena, edge_buffer.size * 6);
    Buffer<RoadSegmentCorners> corner_buffer = buffer_alloc<RoadSegmentCorners>(arena, edge_buffer.size);

    U32 cur_vertex_idx = 0;
    U32 cur_index_idx = 0;
    for (U32 i = 0; i < edge_buffer.size; i++)
    {
        osm::RoadEdge* edge = edge_buffer[i];

        osm::EcefLocation start_node = osm::location_get(network, edge->node_id_from);
        osm::EcefLocation end_node = osm::location_get(network, edge->node_id_to);
        osm::WayNode* way_node = osm::way_find(network, edge->way_id);
        osm::Way* way = &way_node->way;

        F32 road_width = tag_value_get(scratch.arena, S("width"), default_road_width, way->tags);

        RoadSegment road_segment;
        road_segment_from_road_nodes(&road_segment, start_node, end_node, default_road_width);

        osm::RoadEdge* prev_edge = edge->prev;
        if (prev_edge)
        {
            osm::EcefLocation start_node_prev = osm::location_get(network, prev_edge->node_id_from);
            osm::EcefLocation end_node_prev = osm::location_get(network, prev_edge->node_id_to);
            RoadSegment road_segment_prev;
            road_segment_from_road_nodes(&road_segment_prev, start_node_prev, end_node_prev, road_width);
            road_segments_coalesce(&road_segment_prev, &road_segment, road_width);
        }

        osm::RoadEdge* next_edge = edge->next;
        if (next_edge)
        {
            osm::EcefLocation start_node_next = osm::location_get(network, next_edge->node_id_from);
            osm::EcefLocation end_node_next = osm::location_get(network, next_edge->node_id_to);
            RoadSegment road_segment_next;
            road_segment_from_road_nodes(&road_segment_next, start_node_next, end_node_next, road_width);
            road_segments_coalesce(&road_segment, &road_segment_next, road_width);
        }

        // Road coordinates stored in buffer for 3D geometry projection
        RoadSegmentCorners* road_segment_corners = corner_buffer[i];
        road_segment_corners->edge_id = edge->id;
        glm::vec2 local_top_left = glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.top.x, road_segment.start.top.y, road_segment.start.node.pos.z, 1.0));
        glm::vec2 local_top_right = glm::vec2(ecef_to_local * glm::dvec4(road_segment.end.top.x, road_segment.end.top.y, road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_right = glm::vec2(ecef_to_local * glm::dvec4(road_segment.end.btm.x, road_segment.end.btm.y, road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_left = glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.btm.x, road_segment.start.btm.y, road_segment.start.node.pos.z, 1.0));
        road_segment_corners->corners[RoadSegmentCornerCoord_TopLeft] = vec_2f32(local_top_left.x, local_top_left.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_TopRight] = vec_2f32(local_top_right.x, local_top_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomRight] = vec_2f32(local_bottom_right.x, local_bottom_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomLeft] = vec_2f32(local_bottom_left.x, local_bottom_left.y);
        RoadInfo* road_info = map_get(road_info_map, edge->id);
        if (road_info)
        {
            road_segment_corners->road_info = *road_info;
        }

        // add vertex and inde
        quad_to_buffer_add(road_segment_corners, vertex_buffer, index_buffer, edge->id, road_height, &cur_vertex_idx, &cur_index_idx);
    }

    BvhResult result = bvh_create(arena, corner_buffer, 10);

    render::BufferInfo vertex_buffer_info = render::BufferInfo(vertex_buffer, render::BufferType_Vertex);
    render::BufferInfo index_buffer_info = render::BufferInfo(index_buffer, render::BufferType_Index);

    render::Handle vertex_buffer_handle = render::buffer_load_async(&vertex_buffer_info);
    render::Handle index_buffer_handle = render::buffer_load_async(&index_buffer_info);

    city::RoadBuildResult road_build_result = {
        .vertex_buffer_handle = vertex_buffer_handle,
        .index_buffer_handle = index_buffer_handle,
        .bvh_result = result,
    };
    return road_build_result;
}

g_internal Vec3F64
height_dim_add(Vec2F64 pos, F64 height)
{
    Vec3F64 result = vec_3f64(pos.x, pos.y, height);
    return result;
}

g_internal void
quad_to_buffer_add(RoadSegmentCorners* road_segment, Buffer<render::Vertex3DBlend> buffer, Buffer<U32> indices, U64 edge_id, F32 road_height, U32* cur_vertex_idx, U32* cur_index_idx)
{
    F32 road_width = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_TopLeft], road_segment->corners[RoadSegmentCornerCoord_BottomLeft]);
    F32 top_tex_scaled = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_TopLeft], road_segment->corners[RoadSegmentCornerCoord_TopRight]) / road_width;
    F32 btm_tex_scaled = dist_2f32(road_segment->corners[RoadSegmentCornerCoord_BottomLeft], road_segment->corners[RoadSegmentCornerCoord_BottomRight]) / road_width;

    const F32 uv_x_top = 1;
    const F32 uv_x_btm = 0;
    const F32 uv_y_start = 0.5f - (F32)btm_tex_scaled;
    const F32 uv_y_end = 0.5f + (F32)top_tex_scaled;

    U32 base_vertex_idx = *cur_vertex_idx;
    U32 base_index_idx = *cur_index_idx;

    Vec2U32 id = {.u64 = edge_id};
    Vec2F32 blend_factor = vec_2f32(0.0f, 0.0f);

    // quad of vertices
    buffer.data[base_vertex_idx] = {.pos = vec3f32_from_64(height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_TopLeft]), road_height)),
                                    .uv = vec_2f32(uv_x_top, uv_y_start),
                                    .object_id = id,
                                    .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 1] = {.pos = vec3f32_from_64(height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_BottomLeft]), road_height)),
                                        .uv = vec_2f32(uv_x_btm, uv_y_start),
                                        .object_id = id,
                                        .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 2] = {.pos = vec3f32_from_64(height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_TopRight]), road_height)),
                                        .uv = vec_2f32(uv_x_top, uv_y_end),
                                        .object_id = id,
                                        .blend_factor = blend_factor};
    buffer.data[base_vertex_idx + 3] = {.pos = vec3f32_from_64(height_dim_add(vec2f64_from_32(road_segment->corners[RoadSegmentCornerCoord_BottomRight]), road_height)),
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

// ~mgj: Cars
g_internal Rng1F32
car_center_height_offset(Buffer<render::TileVertex> vertices)
{
    F32 highest_value = 0;
    for (U64 i = 0; i < vertices.size; i++)
    {
        highest_value = Max(highest_value, vertices.data[i].pos.y);
    }

    F32 lowest_value = highest_value;
    for (U64 i = 0; i < vertices.size; i++)
    {
        lowest_value = Min(lowest_value, vertices.data[i].pos.y);
    }

    return r1f32(lowest_value, highest_value);
}

g_internal osm::EcefLocation
random_ecef_road_node_get(osm::Network* network)
{
    osm::NodeId node_id = osm::random_node_id_from_type_get(network, osm::WayType::Highway);
    Assert(node_id != 0);
    osm::EcefLocation node_loc = osm::location_get(network, node_id);
    return node_loc;
}

g_internal void
agents_create(AgentSim* agent_sim, osm::Network* network)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    // parse glb file
    String8 glb_path = str8_path_from_str8_list(scratch.arena, {agent_sim->asset_dir, S("bike.glb")});
    gltfw_Result glb_result = gltfw_glb_read(agent_sim->allocator->arena, glb_path);
    // AssertAlways(glb_result.textures.size == 1);
    U32 primitive_count = 0;
    for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
    {
        primitive_count++;
    }
    AssertAlways(primitive_count > 0);

    Vec3F32 model_min = {};
    Vec3F32 model_max = {};
    B32 model_bounds_initialized = false;
    for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
    {
        for (U32 vertex_idx = 0; vertex_idx < node->vertices.size; vertex_idx++)
        {
            Vec3F32 pos = node->vertices.data[vertex_idx].pos;
            if (!model_bounds_initialized)
            {
                model_min = pos;
                model_max = pos;
                model_bounds_initialized = true;
            }
            else
            {
                model_min.x = Min(model_min.x, pos.x);
                model_min.y = Min(model_min.y, pos.y);
                model_min.z = Min(model_min.z, pos.z);
                model_max.x = Max(model_max.x, pos.x);
                model_max.y = Max(model_max.y, pos.y);
                model_max.z = Max(model_max.z, pos.z);
            }
        }
    }

    Vec3F32 model_pivot = {};
    model_pivot.x = (model_min.x + model_max.x) * 0.5f;
    model_pivot.y = model_min.y;
    model_pivot.z = (model_min.z + model_max.z) * 0.5f;

    agent_sim->meshes = buffer_alloc<render::MeshHandlePair>(agent_sim->allocator->arena, primitive_count);
    agent_sim->texture_handles = buffer_alloc<render::Handle>(agent_sim->allocator->arena, glb_result.textures.size);

    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer(render::thread_cmd_buffer_end(thread_ctx));

    for (U32 tex_idx = 0; tex_idx < glb_result.textures.size; ++tex_idx)
    {
        gltfw_Texture* tex = glb_result.textures[tex_idx];
        render::SamplerInfo sampler_info = sampler_from_cgltf_sampler(tex->sampler);
        agent_sim->texture_handles.data[tex_idx] = render::texture_load_sync(thread_ctx, &sampler_info, tex->tex_buf);
    }

    U32 mesh_idx = 0;
    for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
    {
        Assert(node->tex_idx < agent_sim->texture_handles.size);

        // vertex and index extraction
        Buffer<render::TileVertex> vertex_buffer = vertex_3d_from_gltfw_vertex(agent_sim->allocator->arena, node->vertices);
        for (U32 vertex_idx = 0; vertex_idx < vertex_buffer.size; vertex_idx++)
        {
            vertex_buffer.data[vertex_idx].pos.x -= model_pivot.x;
            vertex_buffer.data[vertex_idx].pos.y -= model_pivot.y;
            vertex_buffer.data[vertex_idx].pos.z -= model_pivot.z;
        }
        render::BufferInfo vertex_buffer_info = render::BufferInfo(vertex_buffer, render::BufferType_Vertex);
        Buffer<U32> index_buffer = buffer_arena_copy(agent_sim->allocator->arena, node->indices);
        render::BufferInfo index_buffer_info = render::BufferInfo(index_buffer, render::BufferType_Index);
        agent_sim->meshes.data[mesh_idx].vertex_handle = render::buffer_load_sync(thread_ctx, &vertex_buffer_info);
        agent_sim->meshes.data[mesh_idx].index_handle = render::buffer_load_sync(thread_ctx, &index_buffer_info);
        agent_sim->meshes.data[mesh_idx].texture_handle_idx = node->tex_idx;

        Rng1F32 vertex_center_offset = car_center_height_offset(vertex_buffer);
        if (mesh_idx == 0)
        {
            agent_sim->agent_center_offset = vertex_center_offset;
        }
        else
        {
            agent_sim->agent_center_offset.min = Min(agent_sim->agent_center_offset.min, vertex_center_offset.min);
            agent_sim->agent_center_offset.max = Max(agent_sim->agent_center_offset.max, vertex_center_offset.max);
        }
        mesh_idx++;
    }
    agent_sim->cars = buffer_alloc<Car>(agent_sim->allocator->arena, agent_sim->agent_count);
    agent_sim->agent_map = map_create<WsId, AgentMapItem>(agent_sim->allocator->arena, agent_sim->agent_count);
    agent_sim->agents_active = agent_sim->allocator->place<ArenaArray<Agent>>(agent_sim->max_agent_count);

    for (U32 i = 0; i < agent_sim->agent_count; ++i)
    {
        osm::EcefLocation source_loc = city::random_ecef_road_node_get(network);
        osm::Node* source_node = osm::node_get(network, source_loc.id);
        osm::Node* target_node = osm::random_neighbour_node_get(network, source_node);
        osm::EcefLocation target_loc = osm::location_get(network, target_node->id);
        city::Car* car = &agent_sim->cars.data[i];
        car->source_loc = source_loc;
        car->target_loc = target_loc;
        car->speed = 10.0f;
        car->cur_pos_ecef = source_loc.pos;
        Vec3F64 dir = sub_3f64(target_loc.pos, source_loc.pos);
        car->dir = normalize_3f64(dir);
    }
}

g_internal void
agent_sim_destroy(AgentSim* car_sim)
{
    for (U32 i = 0; i < car_sim->meshes.size; ++i)
    {
        render::handle_destroy(car_sim->meshes.data[i].vertex_handle);
        render::handle_destroy(car_sim->meshes.data[i].index_handle);
    }
    for (U32 i = 0; i < car_sim->texture_handles.size; ++i)
    {
        render::handle_destroy(car_sim->texture_handles.data[i]);
    }

    Allocator::destroy(car_sim->allocator);
}

g_internal Buffer<render::Model3DInstance>
agent_sim_update(Arena* arena, AgentSim* car, osm::Network* network, F64 time_delta, glm::dmat4& ecef_to_local, F32 scale_factor)
{
    prof_scope_marker;
    Buffer<render::Model3DInstance> instance_buffer = buffer_alloc<render::Model3DInstance>(arena, car->cars.size);

    render::Model3DInstance* instance;
    city::Car* car_info;

    F32 car_speed_default = 5.0f; // m/s
    for (U32 agent_idx = 0; agent_idx < car->cars.size; agent_idx++)
    {
        instance = &instance_buffer.data[agent_idx];
        car_info = &car->cars.data[agent_idx];

        Vec3F64 new_pos_3d = add_3f64(car_info->cur_pos_ecef, scale_3f64(scale_3f64(car_info->dir, car_speed_default), time_delta));

        // find out whether target has been reached
        Vec3F64 src_to_target = sub_3f64(car_info->target_loc.pos, car_info->source_loc.pos);
        F64 dist_src_to_target = length_3f64(src_to_target);
        Vec3F64 src_to_new_pos = sub_3f64(new_pos_3d, car_info->source_loc.pos);
        F64 dist_src_to_new_pos = length_3f64(src_to_new_pos);

        // At start up we reset the car's position if target has been by passed by 2 meters
        if ((dist_src_to_new_pos - dist_src_to_target) > 2.0)
        {
            new_pos_3d = car_info->target_loc.pos;
        }

        // Check if the car has reached its destination. If so, find new destination and
        // direction.
        if (dist_src_to_new_pos > dist_src_to_target)
        {
            osm::Node* node = osm::random_neighbour_node_get(network, car_info->target_loc.id);
            osm::EcefLocation new_target_loc = osm::location_get(network, node->id);
            Vec3F64 new_dir = normalize_3f64(sub_3f64(new_target_loc.pos, new_pos_3d));
            car_info->dir = new_dir;
            car_info->source_loc = car_info->target_loc;
            car_info->target_loc = new_target_loc;
        }

        glm::dvec3 z_up = glm::dvec3(0.0f, 0.0f, 1.0f);
        glm::dvec3 dir = glm::normalize(glm::dvec3(ecef_to_local * glm::dvec4(car_info->dir.x, car_info->dir.y, car_info->dir.z, 0.0)));
        glm::dvec3 x_basis = -dir;

        glm::dvec3 z_basis = glm::cross(x_basis, z_up);
        glm::dvec3 y_basis = glm::cross(z_basis, x_basis);

        instance->x_basis = glm::vec4(glm::dvec4(x_basis, 0.0f)) * scale_factor;
        instance->y_basis = glm::vec4(glm::dvec4(y_basis, 0.0f)) * scale_factor;
        instance->z_basis = glm::vec4(glm::dvec4(z_basis, 0.0f)) * scale_factor;
        instance->w_basis = glm::vec4(ecef_to_local * glm::dvec4(new_pos_3d.x, new_pos_3d.y, new_pos_3d.z, 1.0));

        car_info->cur_pos_ecef = new_pos_3d;
    }
    return instance_buffer;
}

g_internal void
agent_sim_update(AgentSim* agent_sim, Buffer<Coordinate> coord_buffer, glm::dmat4& ecef_to_local, F32 scale_factor)
{
    prof_scope_marker;

    ArenaArray<Agent>* agents_active = agent_sim->agents_active;

    glm::dvec3 z_up = glm::dvec3(0.0f, 0.0f, 1.0f);
    for (U32 agent_idx = 0; agent_idx < coord_buffer.size; agent_idx++)
    {
        Coordinate* coord = &coord_buffer.data[agent_idx];
        glm::dvec3 ecef_coord = util::ecef_from_wgs84(coord->lon, coord->lat);
        Agent* agent = {};
        AgentMapItem* agent_ptr = {};
        MapResult result = map_get(agent_sim->agent_map, coord->id, &agent_ptr);
        if (result == MapResult::Success)
        {
            agent = agent_ptr->agent;
            glm::dvec3 ecef_dir = ecef_coord - agent->ecef_coord;

            F64 move_len_sq = glm::dot(ecef_dir, ecef_dir);
            if (move_len_sq > 0.001)
            {
                agent->ecef_coord = ecef_coord;
                agent->ecef_dir = ecef_dir;
            }
        }
        else
        {
            glm::dvec3 local_dir = glm::dvec3(1, 0, 0);
            Agent new_agent = {.ecef_coord = ecef_coord, .ecef_dir = local_dir};
            agent = agents_active->push(new_agent);
            AgentMapItem agent_map_item = {.agent = agent};
            agent_ptr = map_insert(agent_sim->agent_map, coord->id, agent_map_item);
        }

        // model specific orientation
        glm::dvec3 x_basis = -glm::normalize(glm::dvec3(ecef_to_local * glm::dvec4(agent->ecef_dir, 0.0)));
        glm::dvec3 z_basis = glm::cross(x_basis, z_up);
        glm::dvec3 y_basis = glm::cross(z_basis, x_basis);

        agent->model_matrix.x_basis = glm::vec4(glm::dvec4(x_basis, 0.0f)) * scale_factor;
        agent->model_matrix.y_basis = glm::vec4(glm::dvec4(y_basis, 0.0f)) * scale_factor;
        agent->model_matrix.z_basis = glm::vec4(glm::dvec4(z_basis, 0.0f)) * scale_factor;
        agent->model_matrix.w_basis = glm::vec4(ecef_to_local * glm::dvec4(ecef_coord, 1.0));
    }
}
// ~mgj: Buildings

g_internal Buildings*
buildings_create(String8 cache_path, String8 texture_path, Rng2F64 bbox)
{
    prof_scope_marker;

    ScratchScope scratch = ScratchScope(0, 0);
    Arena* arena = arena_alloc();
    Buildings* buildings = PushStruct(arena, Buildings);
    buildings->cache_file_name = push_str8_copy(arena, S("openapi_node_ways_buildings.json"));

    {
        HTTP_RequestParams params = {};
        params.method = HTTP_Method_Post;
        params.content_type = S("text/html");

        String8 query = S(R"(data=
        [out:json] [timeout:25];
        (
          way["building"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )");

        String8 cache_data_file = str8_path_from_str8_list(scratch.arena, {cache_path, buildings->cache_file_name});

        String8 input_str = str8_from_bbox(scratch.arena, bbox);

        Result<String8> cache_read_result = cache_read(scratch.arena, cache_data_file, input_str);
        String8 http_data = cache_read_result.v;
        if (cache_read_result.err)
        {
            // http_data = city_http_call_wrapper(scratch.arena, query_str, &params);
            cache_write(cache_data_file, http_data, input_str);
        }
        osm::RoadNodeParseResult json_result = wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);

        B8 error = true;
        while (error && json_result.error)
        {
            ERROR_LOG("BuildingsCreate: Failed to parse OSM node data from json file\n");
            // http_data = city_http_call_wrapper(scratch.arena, query_str, &params);
            if (http_data.size)
            {
                json_result = wrapper::node_buffer_from_simd_json(scratch.arena, http_data, 100);
                if (json_result.error == false)
                {
                    cache_write(cache_data_file, http_data, input_str);
                    error = false;
                }
            }
        }

        // osm::async_structure_create(json_result.road_nodes, http_data);
    }

    buildings->facade_texture_path = str8_path_from_str8_list(arena, {texture_path, S("brick_wall.ktx2")});
    buildings->roof_texture_path = str8_path_from_str8_list(arena, {texture_path, S("concrete042A.ktx2")});

    return buildings;
}

g_internal void
building_destroy(Buildings* building)
{
    render::handle_destroy(building->roof_model_handles.vertex_buffer_handle);
    render::handle_destroy(building->roof_model_handles.index_buffer_handle);
    render::handle_destroy(building->roof_model_handles.texture_handle);
    render::handle_destroy(building->facade_model_handles.texture_handle);
}
g_internal F64
cross_2f64_z_component(Vec2F64 a, Vec2F64 b)
{
    return a.x * b.y - a.y * b.x;
}
g_internal B32
AreTwoConnectedLineSegmentsCollinear(Vec2F64 prev, Vec2F64 cur, Vec2F64 next)
{
    Vec2F64 ba = sub_2f64(prev, cur);
    Vec2F64 ac = sub_2f64(next, prev);

    F64 cross_product_z = cross_2f64_z_component(ba, ac);
    B32 is_collinear = false;
    if (cross_product_z == 0)
    {
        is_collinear = true;
    }
    return is_collinear;
}

g_internal void
buildings_buffers_create(Arena* arena, osm::Network* osm_network, F32 road_height, glm::dmat4& ecef_to_local, BuildingRenderInfo* out_render_info)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<osm::Way> ways = osm_network->ways_arr[enum_idx(osm::WayType::Building)];
    F32 building_height = 3;

    // ~mgj: Calculate vertex buffer size based on node count
    U32 total_vertex_count = 0;
    U32 total_index_count = 0;

    for (U32 i = 0; i < ways.size; i++)
    {
        osm::Way* way = &ways.data[i];
        // ~mgj: first and last node id should be the same
        U32 way_facade_vertex_count = (way->node_count - 1) * 4;
        total_vertex_count += way_facade_vertex_count + (way->node_count - 1) * 2;
        // ~mgj: count of index for Polyhedron (without ground floor) that makes up the building
        U64 sides_triangle_count = (way->node_count - 1) * 2;
        U64 roof_triangle_count = way->node_count - 2;
        U64 total_triangle_count = sides_triangle_count + roof_triangle_count;
        total_index_count += total_triangle_count * 3;

        Assert(way->node_ids[0] == way->node_ids[way->node_count - 1]);
    }

    Buffer<render::TileVertex> vertex_buffer = buffer_alloc<render::TileVertex>(scratch.arena, total_vertex_count);
    Buffer<U32> index_buffer = buffer_alloc<U32>(scratch.arena, total_index_count);

    U32 base_index_idx = 0;
    U32 base_vertex_idx = 0;
    {
        prof_scope_marker_named("Facade Creation");

        for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
        {
            osm::Way* way = &ways.data[way_idx];

            // ~mgj: Add Vertices and Indices for the sides of building
            for (U32 node_idx = 0, vert_idx = base_vertex_idx, index_idx = base_index_idx; node_idx < way->node_count - 1; node_idx++, vert_idx += 4, index_idx += 6)
            {
                osm::EcefLocation node_loc = osm::location_get(osm_network, way->node_ids[node_idx]);
                osm::EcefLocation next_node_loc = osm::location_get(osm_network, way->node_ids[node_idx + 1]);

                glm::vec3 local_pos = glm::vec3(ecef_to_local * glm::dvec4(node_loc.pos.x, node_loc.pos.y, node_loc.pos.z, 1.0));
                glm::vec3 local_next_pos = glm::vec3(ecef_to_local * glm::dvec4(next_node_loc.pos.x, next_node_loc.pos.y, next_node_loc.pos.z, 1.0));

                F32 side_width = glm::length(local_next_pos - local_pos);
                Vec2U32 id = {.u64 = (U64)way->id};

                vertex_buffer.data[vert_idx] = {.pos = {local_pos.x, local_pos.y, local_pos.z + road_height}, .uv = {0.0f, 0.0f}, .object_id = id};
                vertex_buffer.data[vert_idx + 1] = {.pos = {local_pos.x, local_pos.y, local_pos.z + road_height + building_height}, .uv = {0.0f, building_height}, .object_id = id};
                vertex_buffer.data[vert_idx + 2] = {.pos = {local_next_pos.x, local_next_pos.y, local_next_pos.z + road_height}, .uv = {side_width, 0.0f}, .object_id = id};
                vertex_buffer.data[vert_idx + 3] = {
                    .pos = {local_next_pos.x, local_next_pos.y, local_next_pos.z + road_height + building_height}, .uv = {side_width, building_height}, .object_id = id};

                index_buffer.data[index_idx] = vert_idx;
                index_buffer.data[index_idx + 1] = vert_idx + 1;
                index_buffer.data[index_idx + 2] = vert_idx + 2;
                index_buffer.data[index_idx + 3] = vert_idx + 1;
                index_buffer.data[index_idx + 4] = vert_idx + 2;
                index_buffer.data[index_idx + 5] = vert_idx + 3;
            }

            base_index_idx += (way->node_count - 1) * 6;
            base_vertex_idx += (way->node_count - 1) * 4;
        }
    }

    ///////////////////////////////////////////////////////////////////
    // ~mgj: Create roof
    U32 roof_base_index = base_index_idx;
    {
        prof_scope_marker_named("Roof Creation");
        for (U32 way_idx = 0; way_idx < ways.size; way_idx++)
        {
            osm::Way* way = &ways.data[way_idx];
            Buffer<osm::EcefLocation> buildings_utm_node_buffer = buffer_alloc<osm::EcefLocation>(scratch.arena, way->node_count - 1);
            for (U32 idx = 0; idx < way->node_count - 1; idx += 1)
            {
                buildings_utm_node_buffer.data[idx] = osm::location_get(osm_network, way->node_ids[idx]);
            }

            // ~mgj: ignore collinear line segments
            Buffer<osm::EcefLocation> final_utm_node_buffer = buffer_alloc<osm::EcefLocation>(scratch.arena, buildings_utm_node_buffer.size);
            {
                U32 cur_idx = 0;
                for (U32 idx = 0; idx < buildings_utm_node_buffer.size; idx += 1)
                {
                    Vec3F64 prev_pos3 = buildings_utm_node_buffer.data[(buildings_utm_node_buffer.size + idx - 1) % buildings_utm_node_buffer.size].pos;
                    Vec3F64 cur_pos3 = buildings_utm_node_buffer.data[idx % buildings_utm_node_buffer.size].pos;
                    Vec3F64 next_pos3 = buildings_utm_node_buffer.data[(idx + 1) % buildings_utm_node_buffer.size].pos;
                    Vec2F64 prev_pos = vec_2f64(prev_pos3.x, prev_pos3.y);
                    Vec2F64 cur_pos = vec_2f64(cur_pos3.x, cur_pos3.y);
                    Vec2F64 next_pos = vec_2f64(next_pos3.x, next_pos3.y);

                    B32 is_collinear = AreTwoConnectedLineSegmentsCollinear(prev_pos, cur_pos, next_pos);
                    if (!is_collinear)
                    {
                        final_utm_node_buffer.data[cur_idx++] = buildings_utm_node_buffer.data[idx];
                    }
                }
                final_utm_node_buffer.size = cur_idx;
            }

            // ~mgj: prepare for ear clipping algo
            Buffer<Vec2F64> node_pos_buffer = buffer_alloc<Vec2F64>(scratch.arena, final_utm_node_buffer.size);
            for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
            {
                osm::EcefLocation node_utm = final_utm_node_buffer.data[idx];
                node_pos_buffer.data[idx] = vec_2f64(node_utm.pos.x, node_utm.pos.y);
            }

            Buffer<U32> polygon_index_buffer = EarClipping(scratch.arena, node_pos_buffer);
            if (polygon_index_buffer.size > 0)
            {
                // vertex buffer fill
                for (U32 idx = 0; idx < final_utm_node_buffer.size; idx += 1)
                {
                    osm::EcefLocation node_utm = final_utm_node_buffer.data[idx];
                    glm::vec3 local_pos = glm::vec3(ecef_to_local * glm::dvec4(node_utm.pos.x, node_utm.pos.y, node_utm.pos.z, 1.0));
                    Vec2U32 id = {.u64 = (U64)way->id};
                    vertex_buffer.data[base_vertex_idx + idx] = {
                        .pos = {local_pos.x, local_pos.y, local_pos.z + road_height + building_height},
                        .uv = {local_pos.x, local_pos.y},
                        .object_id = id,
                    };
                }

                // index buffer fill
                for (U32 idx = 0; idx < polygon_index_buffer.size; idx += 1)
                {
                    index_buffer.data[base_index_idx + idx] = polygon_index_buffer.data[idx] + base_vertex_idx;
                }

                base_vertex_idx += final_utm_node_buffer.size;
                base_index_idx += polygon_index_buffer.size;
            }
        }
    }

    {
        prof_scope_marker_named("buildings_buffers_create_buffer_copy");
        Buffer<render::TileVertex> vertex_buffer_final = buffer_alloc<render::TileVertex>(arena, base_vertex_idx);
        Buffer<U32> index_buffer_final = buffer_alloc<U32>(arena, base_index_idx);
        BufferCopy(vertex_buffer_final, vertex_buffer, base_vertex_idx);
        BufferCopy(index_buffer_final, index_buffer, base_index_idx);
        out_render_info->vertex_buffer = vertex_buffer_final;
        out_render_info->index_buffer = index_buffer_final;
    }

    out_render_info->facade_index_offset = 0;
    out_render_info->roof_index_offset = roof_base_index;
    out_render_info->facade_index_count = roof_base_index;
    out_render_info->roof_index_count = base_index_idx - roof_base_index;
}

g_internal void
buildings_build(City* city, osm::Network* osm_network, render::SamplerInfo* sampler_info, glm::dmat4& ecef_to_local, F32 road_height)
{
    Buildings* buildings = &city->buildings;

    render::Handle facade_texture_handle = render::texture_load_async(sampler_info, buildings->facade_texture_path);
    render::Handle roof_texture_handle = render::texture_load_async(sampler_info, buildings->roof_texture_path);

    city::BuildingRenderInfo render_info;
    city::buildings_buffers_create(city->arena, osm_network, road_height, ecef_to_local, &render_info);
    render::BufferInfo vertex_buffer_info = render::BufferInfo(render_info.vertex_buffer, render::BufferType_Vertex);
    render::BufferInfo index_buffer_info = render::BufferInfo(render_info.index_buffer, render::BufferType_Index);

    render::Handle vertex_handle = render::buffer_load_async(&vertex_buffer_info);
    render::Handle index_handle = render::buffer_load_async(&index_buffer_info);

    buildings->roof_model_handles = {.vertex_buffer_handle = vertex_handle,
                                     .index_buffer_handle = index_handle,
                                     .texture_handle = roof_texture_handle,
                                     .index_count = render_info.roof_index_count,
                                     .index_offset = render_info.roof_index_offset};
    buildings->facade_model_handles = {.vertex_buffer_handle = vertex_handle,
                                       .index_buffer_handle = index_handle,
                                       .texture_handle = facade_texture_handle,
                                       .index_count = render_info.facade_index_count,
                                       .index_offset = render_info.facade_index_offset};
}

g_internal Direction
ClockWiseTest(Buffer<Vec2F64> node_buffer)
{
    F64 total = 0;
    for (U32 idx = 0; idx < node_buffer.size; idx += 1)
    {
        Vec2F64 a = node_buffer.data[idx];
        Vec2F64 b = node_buffer.data[(idx + 1) % node_buffer.size];

        F64 cross_product_z = cross_2f64_z_component(a, b);
        total += cross_product_z;
    }
    if (total > 0)
    {
        return Direction_CounterClockwise;
    }
    else if (total < 0)
    {
        return Direction_Clockwise;
    }
    DEBUG_LOG("ClockWiseTest: Lines are collinear\n");
    return Direction_Undefined;
}

g_internal Buffer<U32>
IndexBufferCreate(Arena* arena, U64 buffer_size, Direction direction)
{
    Buffer<U32> index_buffer = buffer_alloc<U32>(arena, buffer_size);
    if (direction == Direction_Clockwise)
    {
        for (U32 i = 0; i < index_buffer.size; i++)
        {
            index_buffer.data[i] = i;
        }
    }
    else if (direction == Direction_CounterClockwise)
    {
        for (U32 i = 0; i < index_buffer.size; i++)
        {
            index_buffer.data[i] = index_buffer.size - i - 1;
        }
    }
    else if (direction == Direction_Undefined)
    {
        Assert(0);
    }
    return index_buffer;
}

// Shoelace Algorithm
// source: https://artofproblemsolving.com/wiki/index.php/Shoelace_Theorem
g_internal B32
PointInTriangle(Vec2F64 p1, Vec2F64 p2, Vec2F64 p3, Vec2F64 point)
{
    F64 d1, d2, d3;
    B32 has_neg, has_pos;

    d1 = cross_2f64_z_component(sub_2f64(point, p1), sub_2f64(p2, p1));
    d2 = cross_2f64_z_component(sub_2f64(point, p2), sub_2f64(p3, p2));
    d3 = cross_2f64_z_component(sub_2f64(point, p3), sub_2f64(p1, p3));

    has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

g_internal void
NodeBufferPrintDebug(Buffer<Vec2F64> node_buffer)
{
    DEBUG_LOG("Error in ear clipping algo. Expecting vertex_count-2 number of triangles\n"
              "The following vertices are the problem: \n");
    for (U32 pt_idx = 0; pt_idx < node_buffer.size; pt_idx++)
    {
        printf("%d, %f, %f\n", pt_idx, node_buffer.data[pt_idx].x, node_buffer.data[pt_idx].y);
    }
}

g_internal Buffer<U32>
EarClipping(Arena* arena, Buffer<Vec2F64> node_buffer)
{
    prof_scope_marker;
    Assert(node_buffer.size >= 3);
    ScratchScope scratch = ScratchScope(&arena, 1);

    U32 total_triangle_count = (node_buffer.size - 2);
    U32 total_index_count = total_triangle_count * 3;

    Direction direction = ClockWiseTest(node_buffer);
    if (direction == Direction_Undefined)
    {
        DEBUG_LOG("Cannot determine direction\n");
        DEBUG_FUNC(NodeBufferPrintDebug(node_buffer));
        return {0, 0};
    }
    Buffer<U32> index_buffer = IndexBufferCreate(scratch.arena, node_buffer.size, direction);
    Buffer<U32> out_vertex_index_buffer = buffer_alloc<U32>(arena, total_index_count);
    U32 cur_index_buffer_idx = 0;
    U32 idx = 0;
    for (; idx < index_buffer.size;)
    {
        if (index_buffer.size < 3)
        {
            break;
        }

        U32 ear_index_buffer_idx = idx % index_buffer.size;
        U32 prev_index_buffer_idx = (index_buffer.size + idx - 1) % index_buffer.size;
        U32 next_index_buffer_idx = (index_buffer.size + idx + 1) % index_buffer.size;

        U32 ear_node_buffer_idx = index_buffer.data[ear_index_buffer_idx];
        U32 prev_node_buffer_idx = index_buffer.data[prev_index_buffer_idx];
        U32 next_node_buffer_idx = index_buffer.data[next_index_buffer_idx];

        Vec2F64 ear = node_buffer.data[ear_node_buffer_idx];
        Vec2F64 prev = node_buffer.data[prev_node_buffer_idx];
        Vec2F64 next = node_buffer.data[next_node_buffer_idx];

        Vec2F64 prev_to_ear = sub_2f64(ear, prev);
        Vec2F64 ear_to_next = sub_2f64(next, ear);

        F64 cross_product_z = cross_2f64_z_component(prev_to_ear, ear_to_next);

        // negative cross product z component means that the triangle has clockwise orientation.
        if (cross_product_z < 0)
        {
            B32 is_ear = true;
            for (U32 test_i = 0; test_i < index_buffer.size - 3; test_i++)
            {
                U32 test_node_buffer_idx = index_buffer.data[(next_index_buffer_idx + test_i + 1) % index_buffer.size];
                Vec2F64 test_point = node_buffer.data[test_node_buffer_idx];

                if (PointInTriangle(prev, ear, next, test_point))
                {
                    is_ear = false;
                    break;
                }
            }

            if (is_ear)
            {
                // add ear to vertex buffer
                out_vertex_index_buffer.data[cur_index_buffer_idx] = prev_node_buffer_idx;
                out_vertex_index_buffer.data[cur_index_buffer_idx + 1] = ear_node_buffer_idx;
                out_vertex_index_buffer.data[cur_index_buffer_idx + 2] = next_node_buffer_idx;
                cur_index_buffer_idx += 3;

                // remove ear from index buffer
                BufferItemRemove(&index_buffer, ear_index_buffer_idx);
                idx = 0;
                continue;
            }
        }
        else if (cross_product_z == 0)
        {
            DEBUG_LOG("EarClipping: Two line segments are collinear");
        }
        idx++;
    }
    if (cur_index_buffer_idx != out_vertex_index_buffer.size)
    {
        DEBUG_FUNC(NodeBufferPrintDebug(node_buffer));
        out_vertex_index_buffer.size = cur_index_buffer_idx;
    }

    Assert(cur_index_buffer_idx == out_vertex_index_buffer.size);
    return out_vertex_index_buffer;
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

    render::SamplerInfo sampler_info = {.min_filter = min_filter, .mag_filter = mag_filter, .mip_map_mode = mipmap_mode, .address_mode_u = address_mode_u, .address_mode_v = address_mode_v};

    return sampler_info;
}

g_internal String8
str8_from_bbox(Arena* arena, Rng2F64 bbox)
{
    String8 str = {.str = (U8*)&bbox, .size = sizeof(Rng2F64)};
    String8 str_copy = push_str8_copy(arena, str);
    return str_copy;
}

g_internal void
road_create(City* city, Road* in_out_road, glm::dmat4& ecef_to_local, String8 area, String8 bbox_cache_str)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    Context* ctx = dt_ctx_get();

    in_out_road->arena = arena_alloc();
    in_out_road->ecef_to_local = ecef_to_local;
    in_out_road->road_height = 10.0f;
    in_out_road->default_road_width = 2.0f;

    constexpr U64 colormap_byte_size = 256ULL * 3 * sizeof(F32);
    U8* zero_arr = PushArray(city->arena, U8, colormap_byte_size);

    in_out_road->colormap_sampler = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_MirroredRepeat,
        .address_mode_v = render::SamplerAddressMode_ClampToEdge,
    };
    U64 node_hashmap_size = 1000;
    U64 way_hashmap_size = 100;
    city->osm_network = osm::osm_init(node_hashmap_size, way_hashmap_size, ctx->data_subdirs.data[dt_DataDirType::Cache], area, bbox_cache_str);

    in_out_road->zero_colormap_handle = render::colormap_load_async(&in_out_road->colormap_sampler, zero_arr, colormap_byte_size);
}

g_internal Map<osm::EdgeId, RoadInfo>*
road_info_from_edge_id(Arena* arena, osm::Network* network, Buffer<osm::RoadEdge> road_edge_buf, Map<S64, neta::EdgeList>* neta_edge_map)
{
    prof_scope_marker;
    Map<osm::EdgeId, RoadInfo>* road_info_map = map_create<osm::EdgeId, RoadInfo>(arena, 1024);

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
            map_insert(road_info_map, edge.id, info);
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
    neta::EdgeList* edge_list = map_get(edge_list_map, way_id);

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

g_internal Buffer<render::TileVertex>
vertex_3d_from_gltfw_vertex(Arena* arena, Buffer<gltfw_Vertex3D> in_vertex_buffer)
{
    Buffer<render::TileVertex> out_vertex_buffer = buffer_alloc<render::TileVertex>(arena, in_vertex_buffer.size);
    for (U32 i = 0; i < in_vertex_buffer.size; i++)
    {
        out_vertex_buffer.data[i].pos = in_vertex_buffer.data[i].pos;
        out_vertex_buffer.data[i].uv = in_vertex_buffer.data[i].uv;
    }
    return out_vertex_buffer;
}

} // namespace city
