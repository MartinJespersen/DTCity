static Buffer<String8>
dt_dir_create(Arena* arena, String8 parent, dt_DataDirPair* dirs, U32 count)
{
    Buffer<String8> buffer = BufferAlloc<String8>(arena, count);
    for (U32 i = 0; i < count; i++)
    {
        String8 dir = str8_path_from_str8_list(arena, {parent, dirs[i].name});
        if (os_file_path_exists(dir) == false)
        {
            B32 dir_created = os_make_directory(dir);
            if (dir_created == false)
            {
                ERROR_LOG("Failed to create directory: %s", dir.str);
            }
        }
        buffer.data[dirs[i].type] = dir;
    }
    return buffer;
}

static void
dt_ctx_set(Context* ctx)
{
    g_ctx = ctx;
}

static Context*
dt_ctx_get()
{
    return g_ctx;
}

static void
dt_time_init(dt_Time* time)
{
    time->last_time_ms = os_now_microseconds();
}

static void
dt_time_update(dt_Time* time)
{
    U64 cur_time = os_now_microseconds();
    time->delta_time_sec = (F64)(cur_time - time->last_time_ms) / 1'000'000.0;
    time->last_time_ms = cur_time;
}

static OS_Handle
dt_render_thread_start(Context* ctx, dt_Input* input)
{
    ctx->running = 1;
    return OS_ThreadLaunch(dt_main_loop, input, NULL);
}

static void
dt_render_thread_join(OS_Handle thread_handle, Context* ctx)
{
    ctx->running = false;
    OS_ThreadJoin(thread_handle, max_U64);
}

static void
CheckVkResult(VkResult result)
{
    if (result != VK_SUCCESS)
        VK_CHECK_RESULT(result);
}

void
dt_imgui_setup(vulkan::Context* vk_ctx, io::IO* io_ctx)
{
    //~mgj: Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    //~mgj: Set Styling
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(io_ctx->window, true);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(
        main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style
                     // scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion =
        VK_API_VERSION_1_3; // Pass in your value of VkApplicationInfo::apiVersion, otherwise will
                            // default to header version.
    init_info.Instance = vk_ctx->instance;
    init_info.PhysicalDevice = vk_ctx->physical_device;
    init_info.Device = vk_ctx->device;
    init_info.QueueFamily = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    init_info.Queue = vk_ctx->graphics_queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPoolSize = 8;
    init_info.MinImageCount = 2;
    init_info.ImageCount = vk_ctx->swapchain_resources->image_count;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &vk_ctx->swapchain_resources->color_format,
        .depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format};
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.UseDynamicRendering = VK_TRUE;
    ImGui_ImplVulkan_Init(&init_info);
}

static dt_Input
dt_interpret_input(int argc, char** argv)
{
    dt_Input input = {};
    Rng2F64* bbox = &input.bbox;
    F64* bbox_coords[4] = {&bbox->min.x, &bbox->min.y, &bbox->max.x, &bbox->max.y};

    if (argc != 1 && argc != 5)
    {
        exit_with_error("Invalid number of command line arguments");
    }

    B8 use_default = true;
    if (argc == 5)
    {
        B8 is_input_malformed = false;
        for (U32 i = 0; (i < (U64)argc) && (i < ArrayCount(bbox_coords)); ++i)
        {
            char* arg = argv[i + 1];
            String8 arg_str = str8_c_string(arg);
            F64 v = f64_from_str8(arg_str);
            if (v == 0.0)
            {
                ERROR_LOG("Invalid input %s passed as cmd line argument\n", arg);
                is_input_malformed = true;
                break;
            }
            *bbox_coords[i] = v;
        }

        if (!is_input_malformed)
        {
            use_default = false;
        }
    }

    if (argc == 1)
    {
        DEBUG_LOG("No command line arguments provided\n");
        use_default = true;
    }

    if (use_default)
    {
        // Initialize default values
        bbox->min.x = 10.298996;
        bbox->min.y = 56.301587;
        bbox->max.x = 10.333500;
        bbox->max.y = 56.322391;
        INFO_LOG("Using default values:\nlon_btm_left=%lf, lat_btm_left=%lf, lon_top_right=%lf, "
                 "lat_top_right=%lf\n",
                 bbox->min.x, bbox->min.y, bbox->max.x, bbox->max.y);
    }
    return input;
}

// wgs84 to utm conversion
g_internal Rng2F64
dt_utm_from_wgs84(Rng2F64 wgs84_bbox)
{
    F64 south_west_lon;
    F64 south_west_lat;
    F64 north_east_lon;
    F64 north_east_lat;
    char utm_zone[10];
    UTM::LLtoUTM(wgs84_bbox.min.y, wgs84_bbox.min.x, south_west_lat, south_west_lon, utm_zone);
    UTM::LLtoUTM(wgs84_bbox.max.y, wgs84_bbox.max.x, north_east_lat, north_east_lon, utm_zone);

    return {south_west_lon, south_west_lat, north_east_lon, north_east_lat};
}

static void
dt_main_loop(void* ptr)
{
    ScratchScope scratch = ScratchScope(0, 0);
    dt_Input* input = (dt_Input*)ptr;

    Context* ctx = dt_ctx_get();
    io::IO* io_ctx = ctx->io;
    os_set_thread_name(str8_c_string("Entrypoint thread"));

    Rng2F64 utm_coords = dt_utm_from_wgs84(input->bbox);
    printf("UTM Coordinates: %f %f %f %f\n", utm_coords.min.x, utm_coords.min.y, utm_coords.max.x,
           utm_coords.max.y);
    render::render_ctx_create(ctx->data_subdirs.data[dt_DataDirType::Shaders], io_ctx,
                              ctx->thread_pool);
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    dt_imgui_setup(vk_ctx, io_ctx);

    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_Repeat,
        .address_mode_v = render::SamplerAddressMode_Repeat,
    };

    U64 node_hashmap_size = 1000;
    U64 way_hashmap_size = 100;
    osm::structure_init(node_hashmap_size, way_hashmap_size, utm_coords);

    String8 cache_dir = ctx->data_subdirs.data[dt_DataDirType::Cache];
    String8 texture_dir = ctx->data_subdirs.data[dt_DataDirType::Texture];
    String8 asset_dir = ctx->data_subdirs.data[dt_DataDirType::Assets];

    osm::Network* osm_network = osm::g_network;
    ui::camera_init(ctx->camera,
                    vec_2f32(osm_network->utm_center_offset.x, osm_network->utm_center_offset.y));

    // ctx->road = city::road_create(texture_dir, cache_dir, ctx->data_dir, input->bbox, utm_coords,
    //                               &sampler_info);
    // city::Road* road = ctx->road;

    // ctx->buildings =
    //     city::buildings_create(cache_dir, texture_dir, 10.f, input->bbox, &sampler_info);
    // city::Buildings* buildings = ctx->buildings;
    // ctx->car_sim = city::car_sim_create(asset_dir, texture_dir, 1000, ctx->road);
    // city::CarSim* car_sim = ctx->car_sim;

    // Initialize Cesium 3D Tiles
    // Use the tileset's geographic location as the origin for the local coordinate system
    // The tileset at C:/ByModel is located at approximately:
    // ECEF: (3502387.3, 634289.2, 5280930.5) -> Geodetic: (10.265119° lon, 56.197934° lat)
    F64 tileset_lon = 10.265119;
    F64 tileset_lat = 56.197934;

    // Load local 3D Tiles tileset
    const char* tileset_url = "file:///C:/ByModel/tileset.json";
    ctx->cesium_tileset = cesium::tileset_renderer_create(
        ctx->arena_permanent, ctx->thread_pool, tileset_url, tileset_lon, tileset_lat, 0.0);

    city::RoadOverlayOption overlay_option_choice = city::RoadOverlayOption_None;

    while (ctx->running)
    {
        dt_time_update(ctx->time);

        arena_clear(dt_ctx_get()->arena_frame);
        io::new_frame();
        render::new_frame();
        ImGui::NewFrame();
        Vec2U32 framebuffer_dim = {(U32)io_ctx->framebuffer_width, (U32)io_ctx->framebuffer_height};

        {
            ui::camera_update(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);
            // U64 hovered_object_id = render::latest_hovered_object_id_get();
            // city::RoadEdge** edge_ptr =
            //     map_get(&road->edge_structure.edge_map, (S64)hovered_object_id);
            // if (edge_ptr)
            // {
            //     city::RoadEdge* edge = *edge_ptr;
            //     osm::WayNode* way_node = osm::way_find(edge->way_id);
            //     osm::Way* way = &way_node->way;

            //     bool open = true;
            //     ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
            //     for (osm::Tag& tag : way->tags)
            //     {
            //         ImGui::Text("%s: %s", (char*)tag.key.str, (char*)tag.value.str);
            //     }

            //     city::RoadInfo* chosen_edge = map_get(road->road_info_map, edge->id);
            //     if (chosen_edge)
            //     {
            //         for (U32 i = 1; i < ArrayCount(chosen_edge->options); i++)
            //         {
            //             ImGui::Text("%s: %lf", city::road_overlay_option_strs[i],
            //                         chosen_edge->options[i]);
            //         }
            //     }
            //     ImVec2 window_size = ImGui::GetWindowSize();
            //     ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
            //     ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

            //     ImGui::End();
            // }
            // osm::WayNode* way_node = osm::way_find(hovered_object_id);
            // if (way_node)
            // {
            //     osm::Way* way = &way_node->way;
            //     bool open = true;
            //     ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
            //     for (U32 tag_idx = 0; tag_idx < way->tags.size; tag_idx += 1)
            //     {
            //         osm::Tag* tag = &way->tags.data[tag_idx];
            //         ImGui::Text("%s: %s", (char*)tag->key.str, (char*)tag->value.str);
            //     }
            //     ImVec2 window_size = ImGui::GetWindowSize();
            //     ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
            //     ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

            //     ImGui::End();
            // }
        }

        ImGui::Begin("Road Overlays", nullptr);
        ImGui::SetWindowPos(ImVec2(0, 0));

        for (U32 i = 0; i < city::RoadOverlayOption_Count; i++)
        {
            ImGui::RadioButton(city::road_overlay_option_strs[i], (int*)&overlay_option_choice,
                               (int)i);
        }
        ImGui::End();

        // city::road_vertex_buffer_switch(road, overlay_option_choice);
        // render::blend_3d_draw(road->handles[road->current_handle_idx]);
        // render::model_3d_draw(buildings->roof_model_handles, false);
        // render::model_3d_draw(buildings->facade_model_handles, false);

        // Buffer<render::Model3DInstance> instance_buffer =
        //     car_sim_update(vk_ctx->draw_frame_arena, car_sim, ctx->time->delta_time_sec);

        // render::BufferInfo instance_buffer_info =
        //     render::buffer_info_from_vertex_3d_instance_buffer(instance_buffer,
        //                                                        render::BufferType_Vertex);

        // render::model_3d_instance_draw(car_sim->texture_handle, car_sim->vertex_handle,
        //                                car_sim->index_handle, &instance_buffer_info);

        // Update and render Cesium 3D Tiles
        printf("Buffer list count: %u\n", vk_ctx->asset_manager->buffer_list.count);
        printf("Buffer Free list count: %u\n", vk_ctx->asset_manager->buffer_free_list.count);
        printf("Texture list count: %u\n", vk_ctx->asset_manager->texture_list.count);
        printf("Texture Free list count: %u\n", vk_ctx->asset_manager->texture_free_list.count);
        if (ctx->cesium_tileset)
        {
            cesium::tileset_update_view(dt_ctx_get()->arena_frame, ctx->cesium_tileset, ctx->camera,
                                        framebuffer_dim, ctx->time->delta_time_sec);
            cesium::tileset_render(ctx->cesium_tileset);
        }

        render::render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera,
                             io_ctx->mouse_pos_cur_s64);

        ImGui::EndFrame();
    }
    render::gpu_work_done_wait();

    // city::road_destroy(ctx->road);
    // city::car_sim_destroy(ctx->car_sim);
    // city::building_destroy(ctx->buildings);
    cesium::tileset_renderer_destroy(ctx->cesium_tileset);
    render::render_ctx_destroy();
}
