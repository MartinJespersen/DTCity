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

template <typename T>
static bool
dt_async_http_check_result(async::AsyncTaskStatus<async::AsyncHttpTaskState<T>>* async_call, String8 name)
{
    B32 done = async_call->done.load(std::memory_order_acquire);
    if (done)
    {
        return true;
    }

    async::AsyncHttpTaskState<T>* task_state = async_call->result;
    const async::AsyncHttpResult& http_result = task_state->http_result;
    switch (http_result.async_result)
    {
        case async::AsyncResult::CurlError:
        {
            exit_with_error("%.*s failed with curl error %u at %s:%u", str8_varg(name), http_result.error_code, task_state->err_file, task_state->err_line);
        }
        break;
        case async::AsyncResult::HttpError:
        {
            exit_with_error("%.*s failed with http error %u at %s:%u", str8_varg(name), http_result.error_code, task_state->err_file, task_state->err_line);
        }
        break;
        case async::AsyncResult::UserFunctionError:
        {
            exit_with_error("%.*s failed: %.*s", str8_varg(name), str8_varg(http_result.error_str));
        }
        break;
    }
    return false;
}

g_internal void
imgui_debug_window(cesium::TilesetRenderer* renderer, async::AsyncHttpTaskCreateResult<neta::NetaTaskState> netascore_result, async::ThreadPool* thread_pool)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    F32 max_debug_window_width = ClampTop(720.0f, viewport->WorkSize.x * 0.6f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(max_debug_window_width, FLT_MAX));

    ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
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
    bool netascore_ready = netascore_result.task_state->done.load(std::memory_order_acquire);
    if (netascore_ready && netascore_result.task_state->error)
    {
        ImGui::Text("Error");
        async::AsyncTaskStatus<neta::NetaTaskState>* netascore_task_state = netascore_result.task_state;
        if (netascore_task_state->ext_type == async::ExtensionType::Http)
        {
            async::AsyncHttpTaskState<neta::NetaTaskState>* http_ext = netascore_task_state->http_ext;
            const async::AsyncHttpResult& http_result = http_ext->http_result;
            const char* user_error_msg = http_result.error_str.size > 0 ? (const char*)http_result.error_str.str : "<missing error message>";
            const char* err_file = http_ext->err_file ? http_ext->err_file : "<unknown file>";
            ImGui::Text("NetaScore Error Code: %u", (U32)http_result.async_result);
            ImGui::TextWrapped("Message: %s", user_error_msg);
            ImGui::TextWrapped("Location: %s:%d", err_file, http_ext->err_line);
            if (http_result.error_code > 0)
            {
                ImGui::Text("Http Error Code: %u", http_result.error_code);
            }
        }
    }
    else if (netascore_ready)
    {
        ImGui::Text("Ready");
    }
    else
    {
        ImGui::Text("Waiting...");
    }
    // camera location
    ImGui::Text("Camera Position: %.2f, %.2f, %.2f", dt_ctx_get()->camera->position.x, dt_ctx_get()->camera->position.y, dt_ctx_get()->camera->position.z);

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

    neta::neta_init(ctx->data_subdirs.data[dt_DataDirType::Cache]);
    draw::draw_init();
    render::render_ctx_create(ctx->data_subdirs.data[dt_DataDirType::Shaders], io_ctx, ctx->thread_pool);
    ui::camera_init(ctx->camera);

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    dt_imgui_setup(vk_ctx, io_ctx);

    // city building ////////////////////////////////////////////
    city::City* city = city::city_init(ctx->data_subdirs.data[dt_DataDirType::Cache]);
    city::city_setup(city, &ctx->cmdline, neta::g_neta_state->netascore_file_path);
    ////////////////////////////////////////////////////////

    city::RoadOverlayOption neta_overlay_option = city::RoadOverlayOption_None;
    while (ctx->running)
    {
        dt_time_update(ctx->time);

        arena_clear(dt_ctx_get()->arena_frame);
        io::new_frame();
        render::new_frame();
        draw::draw_new_frame();
        ImGui::NewFrame();
        async::thread_pool_main_thread_queue_drain(ctx->thread_pool);
        Vec2U32 framebuffer_dim = {(U32)io_ctx->framebuffer_width, (U32)io_ctx->framebuffer_height};
        ui::camera_update(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);

        ImGui::Begin("Road Overlays", nullptr);
        ImGui::SetWindowPos(ImVec2(0, 0));

        for (U32 i = 0; i < city::RoadOverlayOption_Count; i++)
        {
            ImGui::RadioButton(city::road_overlay_option_strs[i], (int*)&neta_overlay_option, (int)i);
        }
        ImGui::End();

        city::city_update(city, ctx->thread_pool, neta_overlay_option, framebuffer_dim);
        // async::async_task_result_done(osm_task_result.task_state, &osm_task_done);
        // async::async_task_result_done(neta_result.task_state, &neta_task_done);

        // // #if BUILD_DEBUG
        // imgui_debug_window(&city->cesium, neta_result, ctx->thread_pool);
        // // #endif

        // if (road_dependents_completed == false && osm::g_network->network_ready.load() && neta::g_neta_state->data_downloaded.load())
        // {
        //     DEBUG_LOG("City building work has been schedule");
        //     CityBuildInput* city_build_input = PushStruct(city->arena, CityBuildInput);
        //     city_build_input->road_state = &city->road;
        //     city_build_input->file_path = push_str8_copy(city->arena, neta_result.task_state->task_state.file_path);
        //     city_build_input->bbox_wgs84 = neta_result.task_state->task_state.bbox_wgs84;

        //     thread_pool_push(city->arena, city_build_input, city_build, ctx->thread_pool);
        //     road_dependents_completed = true;
        // }

        // if (city->road.data_ready.load())
        // {
        //     U64 hovered_object_id = render::latest_hovered_object_id_get();
        //     osm::RoadEdge** edge_ptr = map_get(&osm::g_network->edge_structure.edge_map, (S64)hovered_object_id);
        //     if (edge_ptr)
        //     {
        //         osm::RoadEdge* edge = *edge_ptr;
        //         osm::WayNode* way_node = osm::way_find(edge->way_id);
        //         osm::Way* way = &way_node->way;

        //         bool open = true;
        //         ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
        //         for (osm::Tag& tag : way->tags)
        //         {
        //             ImGui::Text("%s: %s", (char*)tag.key.str, (char*)tag.value.str);
        //         }

        //         city::RoadInfo* chosen_edge = map_get(city->road.road_info_map, edge->id);
        //         if (chosen_edge)
        //         {
        //             for (U32 i = 1; i < ArrayCount(chosen_edge->options); i++)
        //             {
        //                 ImGui::Text("%s: %lf", city::road_overlay_option_strs[i], chosen_edge->options[i]);
        //             }
        //         }
        //         ImVec2 window_size = ImGui::GetWindowSize();
        //         ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
        //         ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

        //         ImGui::End();
        //     }
        //     osm::WayNode* way_node = osm::way_find(hovered_object_id);
        //     if (way_node)
        //     {
        //         osm::Way* way = &way_node->way;
        //         bool open = true;
        //         ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
        //         for (U32 tag_idx = 0; tag_idx < way->tags.size; tag_idx += 1)
        //         {
        //             osm::Tag* tag = &way->tags.data[tag_idx];
        //             ImGui::Text("%s: %s", (char*)tag->key.str, (char*)tag->value.str);
        //         }
        //         ImVec2 window_size = ImGui::GetWindowSize();
        //         ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
        //         ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

        //         ImGui::End();
        //     }
        // }

        // // input:
        // // - Bounding box (in 3D to include height)
        // //  - Using local coordinates from cesium library?
        // // - texture handle of texture to use
        // //

        // // Update and render Cesium 3D Tiles ////////////
        // if (city->cesium)
        // {
        //     cesium::TilesetRenderer* renderer = city->cesium;
        //     cesium::tileset_update_view(dt_ctx_get()->arena_frame, city->cesium, ctx->camera, framebuffer_dim, ctx->time->delta_time_sec);

        //     bool overlay_option_changed = overlay_option_choice != city->road.overlay_option_cur;
        //     B32 road_ready = city->road.data_ready.load();
        //     if (road_ready)
        //     {
        //         city->road.overlay_option_cur = overlay_option_choice;
        //     }
        //     for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
        //     {
        //         if (road_ready)
        //         {
        //             if (tile->compute_scheduled == false || overlay_option_changed)
        //             {
        //                 tile->compute_scheduled = draw::draw_road_intersection_compute(tile->render_data.vertex_buffer_handle, tile->render_data.index_buffer_handle,
        //                 city->road.segment_buffer_handle,
        //                                                                                city->road.segment_node_buffer_handle, overlay_option_choice);
        //             }
        //         }
        //         render::Handle colormap_handle = city->road.overlay_option_cur ? city->road.colormap_handle : city->road.zero_colormap_handle;

        //         if (tile->render_data.bbox_exclude)
        //         {
        //             tile->render_data.bbox_min = {.x = (F32)city->bbox.min.x, .y = (F32)city->bbox.min.y};
        //             tile->render_data.bbox_max = {.x = (F32)city->bbox.max.x, .y = (F32)city->bbox.max.y};
        //         }
        //         draw::draw_model_3d(tile->render_data, colormap_handle);
        //     }

        //     if (cars_creation_scheduled == false && osm::g_network->network_ready.load())
        //     {
        //         Arena* arena = arena_alloc();
        //         CarBuildInput* car_build_input = PushStruct(arena, CarBuildInput);
        //         car_build_input->arena = arena;
        //         car_build_input->city = city;
        //         car_build_input->asset_dir = push_str8_copy(arena, asset_dir);
        //         car_build_input->texture_dir = push_str8_copy(arena, texture_dir);
        //         car_build_input->car_count = 100;

        //         thread_pool_push(arena, car_build_input, car_build, ctx->thread_pool);
        //         cars_creation_scheduled = true;
        //     }

        //     /// car simulation rendering
        //     if (city->car_sim.cars_created.load())
        //     {
        //         render::gpu_work_done_wait();
        //         Buffer<render::Model3DInstance> instance_buffer = car_sim_update(draw::draw_frame_arena_get(), &city->car_sim, ctx->time->delta_time_sec, city->cesium.ecef_to_local);

        //         // instance buffer offset alignment and assignment
        //         render::BufferInfo instance_buffer_info = render::BufferInfo(instance_buffer, render::BufferType_Vertex | render::BufferType_StorageBuffer);
        //         draw::CarInstanceRenderAddResult car_render =
        //             draw::draw_car_instance_render(city->car_sim.vertex_handle, city->car_sim.index_handle, city->car_sim.texture_handle, &instance_buffer_info);

        //         if (car_render.queued)
        //         {
        //             for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
        //             {
        //                 draw::draw_car_instance_compute(&instance_buffer_info, tile->render_data.vertex_buffer_handle, tile->render_data.index_buffer_handle, -city->car_sim.car_center_offset.min,
        //                                                 car_render.instance_buffer_offset);
        //             }
        //         }
        //     }
        // }

        /////////////////////////////////////

        draw::draw_flush();
        render::render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera, io_ctx->mouse_pos_cur_s64);

        ImGui::EndFrame();
        Debug_Frame_End();
    }
    while (thread_pool_has_pending_work(ctx->thread_pool))
    {
    }
    render::gpu_work_done_wait();

    // city::road_destroy(&ctx->road);
    // if (&city->car_sim)
    // {
    //     city::car_sim_destroy(ctx->car_sim);
    // }
    // // city::building_destroy(ctx->buildings);
    // cesium::tileset_renderer_destroy(ctx->cesium_tileset);
    draw::draw_release();
    render::render_ctx_destroy();
}
