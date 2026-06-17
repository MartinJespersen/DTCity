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
    time->last_time_ms = os_now_microseconds();
}

static void
dt_time_update(dt_Time* time)
{
    U64 cur_time = os_now_microseconds();
    F64 delta_time_us = (F64)(cur_time - time->last_time_ms);
    time->delta_time_sec = delta_time_us / 1'000'000.0;
    time->last_time_ms = cur_time;
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
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                                                              .colorAttachmentCount = 1,
                                                              .pColorAttachmentFormats = &vk_ctx->swapchain_resources->color_format,
                                                              .depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format};
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.UseDynamicRendering = VK_TRUE;
    ImGui_ImplVulkan_Init(&init_info);
}

g_internal void
imgui_debug_window(city::City* city, cesium::TilesetRenderer* renderer, async::ThreadPool* thread_pool)
{
    Context* ctx = dt_ctx_get();
    ui::Camera* camera = container_item_from_idx(ctx->camera_container, city->camera_handle);
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    F32 max_debug_window_width = ClampTop(720.0f, viewport->WorkSize.x * 0.6f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(max_debug_window_width, FLT_MAX));

    ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_None);
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::Text("Textures:       %d active, %d free", asset_manager->texture_list.count, asset_manager->texture_free_list.count);
    ImGui::Text("Buffers:        %d active, %d free", asset_manager->buffer_list.count, asset_manager->buffer_free_list.count);
    for (U32 i = 0; i < ArrayCount(asset_manager->deletion_queues); i++)
    {
        ImGui::Text("Deletion Queue %d: %d active", i, asset_manager->deletion_queues[i].list_count);
    }
    ImGui::Text("Deletetion Queue Free List: %d active", asset_manager->deletion_queue_free_list_count);
    ImGui::Text("Tileset Renderer Show: %d active", renderer->tiles_to_show_count);
    ImGui::Text("Tileset Renderer Free List Count: %d", renderer->tiles_to_free_stack_count);
    ImGui::Text("ThreadPool pending tasks: %u", thread_pool->pending_task_count.load());

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
        ImGui::Text("Heap %u%s: %llu / %llu MB used, %llu MB free", i, device_local ? " device" : "", usage_mb, budget_mb, free_mb);
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

    // city building ////////////////////////////////////////////
    const city::CityInfo cities_info_arr[] = {{.name = S("Aarhus"),
                                               .lon = 10.291206,
                                               .lat = 56.253108,
                                               .bbox_width_meters = 5000,
                                               .bbox_height_meters = 5000,
                                               .tileset_path = S("file:///C:/ByModel/5km_6235_580/tileset.json"),
                                               .bbox_clipping_enabled = true,
                                               .custom_geometry_enabled = true},
                                              {.name = S("Eskiltuna"),
                                               .lon = 16.49952138067,
                                               .lat = 59.36163877297,
                                               .bbox_width_meters = 5000,
                                               .bbox_height_meters = 5000,
                                               .tileset_path = S("file:///C:/ByModel/eskiltuna/Totalstad_2025_q3/tileset.json")}};

    Buffer<city::City> city_buf = buffer_alloc<city::City>(ctx->arena_main_permanent, ArrayCount(cities_info_arr));
    for (U32 i = 0; i < city_buf.size; ++i)
    {
        const city::CityInfo* city_config = &cities_info_arr[i];
        city::City* city = city_buf[i];

        city->camera_handle = container_array_idx_get(ctx->camera_container);
        ui::Camera* camera = container_item_from_idx(ctx->camera_container, city->camera_handle);
        ui::camera_init(ctx->arena_main_permanent, camera);

        Rng2F64 bbox = util::wgs84_bbox_from_btm_right_corner(city_config->lon, city_config->lat, city_config->bbox_width_meters, city_config->bbox_height_meters);
        city::city_init(city, ctx->data_subdirs.data[dt_DataDirType::Cache]);
        city->bbox = bbox;
        city->tileset_url = city_config->tileset_path;
        city::city_build(city, city_config, bbox, city_config->tileset_path, city_config->name);
        ////////////////////////////////////////////////////////
    }

    async::AsyncWebsocketCreateResult ws_task_result = async::async_websocket_start(S("ws://127.0.0.1:8080/ws"));
    if (ws_task_result.has_error())
    {
        ERROR_LOG("Error from websocket");
    }

    city::RoadOverlayOption neta_overlay_option = city::RoadOverlayOption_None;
    S32 cur_area_option = 0;
    S32 area_option = cur_area_option;

    const city::CityInfo* city_info = &cities_info_arr[cur_area_option];
    city::City* city = city_buf[cur_area_option];
    while (ctx->running)
    {
        dt_time_update(ctx->time);

        arena_clear(dt_ctx_get()->arena_frame);
        io::new_frame();
        render::new_frame();
        draw::draw_new_frame();
        ImGui::NewFrame();
        async::thread_pool_main_thread_queue_drain(ctx->thread_pool);

        if (!ws_task_result.has_error())
        {
            String8List ws_msgs = async::async_websocket_read(ctx->arena_frame, ws_task_result.ws_session);
            for (String8Node* node = ws_msgs.first; node; node = node->next)
            {
                INFO_LOG("ws msg: %s", node->string.str);
            }
        }
        Vec2U32 framebuffer_dim = {(U32)io_ctx->framebuffer_width, (U32)io_ctx->framebuffer_height};

        ImGui::Begin("Interaction", nullptr);

        ImGui::SeparatorText("Area");
        for (U32 i = 0; i < ArrayCount(cities_info_arr); i++)
        {
            ImGui::RadioButton((const char*)cities_info_arr[i].name.str, (int*)&area_option, (int)i);
        }

        ImGui::SeparatorText("Netascore");
        ImGui::SetWindowPos(ImVec2(0, 0));

        for (U32 i = 0; i < city::RoadOverlayOption_Count; i++)
        {
            ImGui::RadioButton(city::road_overlay_option_strs[i], (int*)&neta_overlay_option, (int)i);
        }

        ImGui::SeparatorText("Agent Size");
        city::City* selected_city = city_buf[area_option];
        ImGui::SliderFloat("Scale", &selected_city->car_scale_factor, 0.01f, 0.1f, "%.3f");

        ImGui::End();

        if (cur_area_option != area_option)
        {
            cur_area_option = area_option;
            city = city_buf[cur_area_option];
            city_info = &cities_info_arr[cur_area_option];
        }

        ui::Camera* camera = container_item_from_idx(ctx->camera_container, city->camera_handle);
        ImGuiIO& imgui_io = ImGui::GetIO();
        bool imgui_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        bool imgui_input_captured = imgui_io.WantCaptureMouse || imgui_io.WantCaptureKeyboard;
        bool world_camera_enable = !imgui_window_hovered && !imgui_input_captured;
        ui::camera_update(camera, ctx->io, ctx->time->delta_time_sec, vec_2s32(io_ctx->framebuffer_width, io_ctx->framebuffer_height), vk_ctx->current_frame, world_camera_enable);
        // keep inactive cities' tilesets making progress so their raster overlay
        // tile providers finish creating in the background; the active city is
        // pumped by city_update -> tileset_update_view
        for (U32 i = 0; i < city_buf.size; ++i)
        {
            cesium::tileset_pump_async(&city_buf[i]->cesium);
        }
        city::city_update(city, ctx->thread_pool, neta_overlay_option, framebuffer_dim, city_info);

        // #if BUILD_DEBUG
        imgui_debug_window(city, &city->cesium, ctx->thread_pool);
        // #endif

        /////////////////////////////////////

        draw::draw_flush();
        render::render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, io_ctx->mouse_pos_cur_s64);

        ImGui::EndFrame();
        Debug_Frame_End();
    }
    while (thread_pool_has_pending_work(ctx->thread_pool))
    {
    }
    render::gpu_work_done_wait();
    draw::draw_release();
    render::render_ctx_destroy();
}
