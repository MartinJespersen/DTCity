static Buffer<String8>
dt_dir_create(Arena* arena, String8 parent, dt_DataDirPair* dirs, U32 count)
{
    Buffer<String8> buffer = buffer_alloc<String8>(arena, count);
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
}

static void
dt_time_update(io::IO* io, dt_Time* time)
{
    constexpr F64 min_refresh_rate = 10.0;
    constexpr F64 max_refresh_rate = 250.0;

    U64 prev_timestamp = time->frame_timestamp_ms;
    time->frame_timestamp_ms = (F64)os_now_microseconds();
    U64 time_delta_ms = time->frame_timestamp_ms - prev_timestamp;

    time->frame_timestamp_delta_ms =
        Clamp(1'000'000 / max_refresh_rate, (F64)time_delta_ms, 1'000'000 / min_refresh_rate);

    F64 refresh_rate = Clamp(min_refresh_rate, (F64)io->frame_rate.load(), max_refresh_rate);
    time->time_delta_constant_sec = 1.0 / refresh_rate;
}

static OS_Handle
dt_render_thread_start(Context* ctx)
{
    ctx->running = 1;
    return OS_ThreadLaunch(dt_main_loop, NULL, NULL);
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
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style
                                     // scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3; // Pass in your value of VkApplicationInfo::apiVersion, otherwise will
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

g_internal void
imgui_debug_window(city::City* city, async::ThreadPool* thread_pool)
{
    Context* ctx = dt_ctx_get();
    ui::Camera* camera = {};
    B32 camera_exists = ctx->camera_container->item_from_handle(city->camera_handle, &camera);
    Assert(camera_exists);
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    F32 max_debug_window_width = ClampTop(720.0f, viewport->WorkSize.x * 0.6f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(max_debug_window_width, FLT_MAX));

    ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_None);
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::Text("VSync FPS: %d", ctx->io->frame_rate.load());
    ImGui::Text("Textures:       %d active, %d free", asset_manager->texture_list.count,
                asset_manager->texture_free_list.count);
    ImGui::Text("Buffers:        %d active, %d free", asset_manager->buffer_list.count,
                asset_manager->buffer_free_list.count);
    for (U32 i = 0; i < ArrayCount(asset_manager->deletion_queues); i++)
    {
        ImGui::Text("Deletion Queue %d: %d active", i, asset_manager->deletion_queues[i].list_count);
    }
    ImGui::Text("Deletetion Queue Free List: %d active", asset_manager->deletion_queue_free_list_count);
    ImGui::Text("ThreadPool pending tasks: %u", thread_pool->pending_task_count.load());
    city::tile_load_debug_ui_draw(ctx->tile_load_state, city->tileset_handle);

    // netascore status
    ImGui::Text("Netascore Status: ");
    if (city->neta_task_done)
    {
        ImGui::Text("Ready");
    }
    else
    {
        ImGui::Text("Waiting...");
    }

    // osm status
    ImGui::Text("Osm Status: ");
    if (city->osm_task_done)
    {
        ImGui::Text("Ready");
    }
    else
    {
        ImGui::Text("Waiting...");
    }

    // camera location
    ImGui::Text("Camera Position: %.2f, %.2f, %.2f", camera->position.x, camera->position.y, camera->position.z);

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(asset_manager->allocator, budgets);
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(vk_ctx->physical_device, &memory_properties);
    for (U32 i = 0; i < memory_properties.memoryHeapCount; i++)
    {
        B32 device_local = memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        U64 budget_mb = budgets[i].budget / MB(1);
        U64 usage_mb = budgets[i].usage / MB(1);
        U64 free_mb = 0;
        if (budgets[i].budget > budgets[i].usage)
        {
            free_mb = (budgets[i].budget - budgets[i].usage) / MB(1);
        }
        ImGui::Text("Heap %u%s: %llu / %llu MB used, %llu MB free", i, device_local ? " device" : "", usage_mb,
                    budget_mb, free_mb);
    }

    ImGui::End();
}

static void
dt_main_loop(void* ptr)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Context* ctx = dt_ctx_get();
    io::IO* io_ctx = ctx->io;
    os_set_thread_name(str8_c_string("Entrypoint thread"));
    AssertAlways(async::thread_pool_register_current_thread(ctx->thread_pool));

    draw::draw_init();
    render::render_ctx_create(ctx->data_subdirs.data[dt_DataDirType::Shaders], io_ctx, ctx->thread_pool);

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    dt_imgui_setup(vk_ctx, io_ctx);

    constexpr F32 agent_hover_icon_radius = 1.0f;
    constexpr F32 agent_hover_icon_height = 2.0f;
    render::PrimitiveMesh agent_hover_icon_mesh = geometry::hover_icon_mesh_create(
        ctx->arena, agent_hover_icon_radius, agent_hover_icon_height, 0.0f, city::agent_hover_icon_color);
    render::MeshHandle agent_hover_icon_mesh_handle = render::mesh_handles_create_and_upload(agent_hover_icon_mesh);

    // city building ////////////////////////////////////////////
    const city::AreaConfig cities_info_arr[] = {{.name = S("Aarhus"),
                                                 .coordinate_type = city::AreaCoordinateType::Utm,
                                                 .utm = {575000, 6220000, 32},
                                                 .bbox_width_meters = 5000,
                                                 .bbox_height_meters = 5000,
                                                 .tileset_source = city::AreaTilesetSource::Path,
                                                 .tileset_path = S("file:///C:/ByModel/Aarhus_MeshData/tileset.json"),
                                                 .bbox_clipping_enabled = true,
                                                 .custom_geometry_enabled = true},
                                                {.name = S("Eskiltuna"),
                                                 .wgs84 = {16.49952138067, 59.36163877297},
                                                 .bbox_width_meters = 5000,
                                                 .bbox_height_meters = 5000},
                                                {.name = S("Zurich"),
                                                 .wgs84 = {8.532010538692882, 47.40024260563559},
                                                 .bbox_width_meters = 5000,
                                                 .bbox_height_meters = 5000},
                                                {.name = S("Aarhus (ion)"),
                                                 .coordinate_type = city::AreaCoordinateType::Utm,
                                                 .utm = {575000, 6220000, 32},
                                                 .bbox_width_meters = 5000,
                                                 .bbox_height_meters = 5000,
                                                 .tileset_source = city::AreaTilesetSource::IonAsset,
                                                 .tileset_ion_asset_id = 5898353,
                                                 .bbox_clipping_enabled = true,
                                                 .custom_geometry_enabled = true}};

    ctx->tile_load_state = city::tile_load_create(ctx->thread_pool, ArrayCount(cities_info_arr));
    Buffer<city::City> city_buf = buffer_alloc<city::City>(ctx->arena, ArrayCount(cities_info_arr));
    for (U32 i = 0; i < city_buf.size; ++i)
    {
        const city::AreaConfig* city_config = &cities_info_arr[i];
        city::City* city = &city_buf[i];

        ui::Camera* camera = {};
        city->camera_handle = ctx->camera_container->item_new(&camera);
        ui::camera_init(ctx->arena, camera);

        Vec2F64 wgs84 = city::city_area_wgs84_get(city_config);
        Rng2F64 bbox = util::wgs84_bbox_from_btm_right_corner(wgs84.x, wgs84.y, city_config->bbox_width_meters,
                                                              city_config->bbox_height_meters);
        city::city_init(city, ctx->data_subdirs.data[dt_DataDirType::Cache]);
        city->bbox = bbox;
        String8 tileset_path = {};
        if (city_config->tileset_source == city::AreaTilesetSource::Path)
        {
            tileset_path = city_config->tileset_path;
        }
        city::city_build(city, bbox, tileset_path, city_config->name);
        ////////////////////////////////////////////////////////
    }

    city::Simulation simulator = {};
    city::SimulationError simulator_connection_error = simulator.start();
    if (simulator_connection_error.type != city::SimulationErrorType::Success)
    {
        exit_with_error("Error during simulator setup");
    }
    U32 simulator_scenario_idx = 0;
    city::RoadOverlayOption neta_overlay_option = city::RoadOverlayOption_None;
    S32 cur_area_option = 1;
    S32 area_option = cur_area_option;
    B32 area_switch_pending = false;

    const city::AreaConfig* area_config = &cities_info_arr[cur_area_option];
    city::City* area = &city_buf[cur_area_option];

    city_area_streaming_begin(area, area_config);

    F64 playback_time = 0;
    F64 fetch_period_seconds = 10;
    bool playback_running = true;
    bool agent_snapshot_needed = true;
    while (ctx->running)
    {
        dt_time_update(ctx->io, ctx->time);

        arena_clear(dt_ctx_get()->arena_frame);
        io::new_frame(ctx->io);
        render::new_frame();
        draw::draw_new_frame();
        ImGui::NewFrame();
        async::thread_pool_main_thread_queue_drain(ctx->thread_pool);

        String8List msg_list = {};
        String8 ws_msg = {};
        static U32 message_frame_counter = 0;
        if (message_frame_counter % 60 == 0)
        {
            ws_msg = push_str8f(ctx->arena_frame, "test message to server: %u\n", message_frame_counter);
            str8_list_push(ctx->arena_frame, &msg_list, ws_msg);
        }
        message_frame_counter++;

        Vec2U32 framebuffer_dim = {(U32)io_ctx->framebuffer_width, (U32)io_ctx->framebuffer_height};
        city::ServerUpdate server_update = {.playback = playback_time, .period = fetch_period_seconds};
        bool playback_changed = false;

        ImGui::PushFont(nullptr, 18);
        ImGui::PushFont(nullptr, 24.0f);
        ImGui::Begin("Interaction", nullptr);
        ImGui::PopFont();

        ImGui::PushFont(nullptr, 22.0f);
        ImGui::SeparatorText("Scenarios");
        ImGui::PopFont();

        if (area->cars_creation_done)
        {
            if (simulator_scenario_idx > simulator.metadata.scenarios.size())
            {
                simulator_scenario_idx = 0;
            }

            // Index zero is the UI-only "None" entry; received scenarios start at one.
            for (U32 scenario_idx = 0; scenario_idx <= simulator.metadata.scenarios.size(); ++scenario_idx)
            {
                const char* scenario_name = "None";
                if (scenario_idx > 0)
                {
                    scenario_name = simulator.metadata.scenarios[scenario_idx - 1].name.c_str();
                }
                ImGui::PushID((int)scenario_idx);
                bool selected = simulator_scenario_idx == scenario_idx;
                bool clicked = ImGui::RadioButton(scenario_name, selected);
                if (clicked)
                {
                    playback_changed = simulator_scenario_idx != scenario_idx;
                    simulator_scenario_idx = scenario_idx;
                    if (scenario_idx > 0 && playback_changed)
                    {
                        playback_time = simulator.metadata.scenarios[scenario_idx - 1].timestamp_start;
                    }
                }
                ImGui::PopID();
            }

            F64 delta_seconds = ctx->time->frame_timestamp_delta_ms / 1'000'000.0;
            if (simulator_scenario_idx > 0)
            {
                const city::Scenario& scenario = simulator.metadata.scenarios[simulator_scenario_idx - 1];
                ImGui::Checkbox("Play", &playback_running);
                if (playback_running && !playback_changed)
                {
                    playback_time = city::simulator_playback_advance(playback_time, delta_seconds,
                                                                     scenario.timestamp_end);
                }
                bool seek = ImGui::SliderScalar("Timestamp", ImGuiDataType_Double, &playback_time,
                                                &scenario.timestamp_start, &scenario.timestamp_end, "%.3f");
                playback_changed = playback_changed || seek;
                server_update.name = scenario.name;
            }
            F64 min_period = 0.1;
            F64 max_period = 60;
            ImGui::SliderScalar("Fetch period (seconds)", ImGuiDataType_Double, &fetch_period_seconds, &min_period,
                                &max_period, "%.1f");
            server_update.playback = playback_time;
            server_update.period = fetch_period_seconds;
        }

        ImGui::PushFont(nullptr, 22.0f);
        ImGui::SeparatorText("Area");
        ImGui::PopFont();
        for (U32 i = 0; i < ArrayCount(cities_info_arr); i++)
        {
            ImGui::RadioButton((const char*)cities_info_arr[i].name.str, (int*)&area_option, (int)i);
        }

        if (area_switch_pending)
        {
            ImGui::TextUnformatted("Switching city...");
        }

        ImGui::PushFont(nullptr, 22.0f);
        ImGui::SeparatorText("NetAScore");
        ImGui::PopFont();

        ImGui::SetWindowPos(ImVec2(0, 0));

        for (U32 i = 0; i < city::RoadOverlayOption_Count; i++)
        {
            ImGui::RadioButton(city::road_overlay_option_strs[i], (int*)&neta_overlay_option, (int)i);
        }

        {
            prof_scope_marker_named("Scroll Agent Time");
            ImGui::SeparatorText("Agent Size");
            city::City* selected_city = &city_buf[area_option];
            ImGui::SliderFloat("Scale", &selected_city->all_agent_scale_factor, 0.01f, 100.0f, "%.3f");
        }

        ImGui::End();
        ImGui::PopFont();

        // Finish an initiated teardown even if the user selects the old city
        // again; then open the latest selection with a fresh renderer.
        if (cur_area_option != area_option || area_switch_pending)
        {
            area_switch_pending = true;
            B32 streaming_ended = city_area_streaming_end(area);
            if (streaming_ended)
            {
                Debug_Frame_End();
                Debug_Memory_Snapshot_Dump();

                cur_area_option = area_option;
                area = &city_buf[cur_area_option];
                area_config = &cities_info_arr[cur_area_option];

                city_area_streaming_begin(area, area_config);
                area_switch_pending = false;
                // The new area's agent pool needs a fresh complete snapshot.
                agent_snapshot_needed = true;
            }
        }

        draw::draw_camera_set(area->camera_handle);

        ImGuiIO& imgui_io = ImGui::GetIO();
        bool imgui_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        bool imgui_input_captured = imgui_io.WantCaptureMouse || imgui_io.WantCaptureKeyboard;
        bool world_camera_enable = !imgui_window_hovered && !imgui_input_captured;
        area->no_gui_focus = false;
        if (!world_camera_enable)
        {
            area->no_gui_focus = true;
        }

        draw::DrawFrame* frame = draw::draw_frame_get();
        {
            ui::Camera* camera = {};
            if (ctx->camera_container->item_from_handle(frame->camera_resource_handle, &camera))
            {
                ui::camera_update(camera, ctx->io, ctx->time->frame_timestamp_delta_ms / 1'000'000,
                                  vec_2s32(io_ctx->framebuffer_width, io_ctx->framebuffer_height), world_camera_enable);
            }
            city::city_update(area, ctx->thread_pool, neta_overlay_option, framebuffer_dim, area_config);

            // Gather agent batches using the displayed area and this frame's camera.
            if (area->cars_creation_done && !area_switch_pending)
            {
                city::Simulation::Update updates =
                    simulator.update(ctx->arena_frame, server_update, playback_changed || agent_snapshot_needed);
                agent_snapshot_needed = false;
                cesium::TilesetRenderer* tileset = {};
                B32 tileset_found =
                    ctx->tile_load_state->tileset_pool->item_from_handle(area->tileset_handle, &tileset);
                if (tileset_found && !tileset->destruction_requested)
                {
                    area->agent_system->update(camera, agent_hover_icon_mesh_handle, tileset,
                                               updates.updates, updates.agents_clear, updates.snapshot_received,
                                               area->all_agent_scale_factor, tileset->ecef_to_local);
                }
            }

            // Build every ImGui window before render_frame ends the ImGui frame.
            imgui_debug_window(area, ctx->thread_pool);

            render::MappedHandle<void> camera_handle_void = render::mapped_handle_erased(camera->mut_handles);
            render::render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, io_ctx->mouse_pos_cur_s64,
                                 camera_handle_void);
        }

        /////////////////////////////////////

        ImGui::EndFrame();
        Debug_Frame_End();
    }
#if GRACEFUL_SHUTDOWN
    for (;;)
    {
        async::thread_pool_main_thread_queue_drain(ctx->thread_pool);
        render::gpu_work_update();
        bool pending_work = thread_pool_has_pending_work(ctx->thread_pool);
        if (!pending_work)
        {
            // A worker may have queued a callback just before finishing.
            async::thread_pool_main_thread_queue_drain(ctx->thread_pool);
            pending_work = thread_pool_has_pending_work(ctx->thread_pool);
            if (!pending_work) break;
        }
    }
    render::gpu_work_update();
    render::gpu_work_done_wait();
    render::gpu_work_update();
    for (ui::Camera& camera : *ctx->camera_container)
    {
        render::mapped_buffer_destroy(camera.mut_handles);
    }
    for (U32 i = 0; i < city_buf.size; i += 1)
    {
        city::city_release(&city_buf[i]);
    }
    render::handle_destroy(agent_hover_icon_mesh_handle.vertex_buffer_handle);
    render::handle_destroy(agent_hover_icon_mesh_handle.index_buffer_handle);
    city::tile_load_destroy(ctx->tile_load_state);
    draw::draw_release();
    render::render_ctx_destroy();
    Debug_Frame_End();
    Debug_Memory_Snapshot_Dump();
#endif
}
