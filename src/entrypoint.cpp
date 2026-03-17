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
    F64 delta_time_us = (F64)(cur_time - time->last_time_ms);
    time->delta_time_sec = delta_time_us / 1'000'000.0;
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
        bbox->min.x = 10.291206;
        bbox->min.y = 56.253108;
        bbox->max.x = 10.37349;
        bbox->max.y = 56.29715;
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

g_internal void
imgui_debug_window(cesium::TilesetRenderer* renderer)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));

    ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::Text("Textures:       %d active, %d free", asset_manager->texture_list.count, asset_manager->texture_free_list.count);
    ImGui::Text("Buffers:        %d active, %d free", asset_manager->buffer_list.count, asset_manager->buffer_free_list.count);
    ImGui::Text("Descriptor Sets: %d active, %d free", asset_manager->descriptor_set_list.count, asset_manager->descriptor_set_free_list.count);
    for (U32 i = 0; i < ArrayCount(asset_manager->deletion_queues); i++)
    {
        ImGui::Text("Deletion Queue %d: %d active", i, asset_manager->deletion_queues[i].list_count);
    }
    ImGui::Text("Deletetion Queue Free List: %d active", asset_manager->deletion_queue_free_list_count);
    ImGui::Text("Tileset Renderer Show: %d active", renderer->tiles_to_show_count);
    ImGui::Text("Tileset Renderer Free List Count: %d", renderer->tiles_to_free_stack_count);

    ImGui::End();
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
    printf("UTM Coordinates: %f %f %f %f\n", utm_coords.min.x, utm_coords.min.y, utm_coords.max.x, utm_coords.max.y);
    render::render_ctx_create(ctx->data_subdirs.data[dt_DataDirType::Shaders], io_ctx, ctx->thread_pool);
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
    osm::structure_init(node_hashmap_size, way_hashmap_size);

    String8 cache_dir = ctx->data_subdirs.data[dt_DataDirType::Cache];
    String8 texture_dir = ctx->data_subdirs.data[dt_DataDirType::Texture];
    String8 asset_dir = ctx->data_subdirs.data[dt_DataDirType::Assets];

    ui::camera_init(ctx->camera);

    // Initialize Cesium 3D Tiles
    // Use the tileset's geographic location as the origin for the local coordinate system
    // The tileset at C:/ByModel is located at approximately:
    // ECEF: (3502387.3, 634289.2, 5280930.5) -> Geodetic: (10.265119° lon, 56.197934° lat)
    F64 tileset_lon = 10.3013;
    F64 tileset_lat = 56.2621;

    // Load local 3D Tiles tileset
    // const char* tileset_url = "file:///C:/ByModel/5km_6235_580/tileset.json";
    const char* tileset_url = "file:///mnt/c/ByModel/5km_6235_580/tileset.json";

    ctx->buildings = city::buildings_create(cache_dir, texture_dir, input->bbox);

    city::RoadOverlayOption overlay_option_choice = city::RoadOverlayOption_None;

    ctx->road = city::road_create(texture_dir, cache_dir, ctx->data_dir, input->bbox, utm_coords, &sampler_info);

    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(tileset_lon), glm::radians(tileset_lat), 0);
    CesiumGeospatial::LocalHorizontalCoordinateSystem* local_coord = new CesiumGeospatial::LocalHorizontalCoordinateSystem(
        origin_cartographic, CesiumGeospatial::LocalDirection::East, CesiumGeospatial::LocalDirection::North, CesiumGeospatial::LocalDirection::Up);

    glm::dmat4 ecef_to_local = local_coord->getEcefToLocalTransformation();

    ctx->road->road_build_result =
        city::road_segment_build(ctx->road->arena, ctx->road->edge_structure.edges, ctx->road->default_road_width, ctx->road->road_height, ecef_to_local, ctx->road->road_info_map);

    render::BufferInfo road_segment_buffer_info = render::BufferInfo(ctx->road->road_build_result.bvh_result.road_segment_buffer_sorted, render::BufferType_StorageBuffer);
    render::Handle road_segment_buffer_handle = render::buffer_load_async(&road_segment_buffer_info);

    render::BufferInfo road_segment_node_buffer_info = render::BufferInfo(ctx->road->road_build_result.bvh_result.node_buffer, render::BufferType_StorageBuffer);
    render::Handle road_segment_node_buffer_handle = render::buffer_load_async(&road_segment_node_buffer_info);

    render::Handle road_segment_handle = render::road_segment_descriptor_load_async(ctx->arena_permanent, road_segment_buffer_handle, road_segment_node_buffer_handle);

    ctx->car_sim = city::car_sim_create(asset_dir, texture_dir, 100);
    render::gpu_work_done_wait();

    ctx->cesium_tileset = cesium::tileset_renderer_create(ctx->arena_permanent, ctx->thread_pool, tileset_url, tileset_lon, tileset_lat, 0.0);
    // city::Buildings* buildings = ctx->buildings;
    // buildings_build(buildings, &sampler_info, ecef_to_local, ctx->road->road_height);

    while (ctx->running)
    {
        dt_time_update(ctx->time);

        arena_clear(dt_ctx_get()->arena_frame);
        io::new_frame();
        render::new_frame();
        ImGui::NewFrame();

#if BUILD_DEBUG
        imgui_debug_window(ctx->cesium_tileset);
#endif
        Vec2U32 framebuffer_dim = {(U32)io_ctx->framebuffer_width, (U32)io_ctx->framebuffer_height};

        {
            ui::camera_update(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);
            U64 hovered_object_id = render::latest_hovered_object_id_get();
            printf("ID: %llu\n", hovered_object_id);
            city::RoadEdge** edge_ptr = map_get(&ctx->road->edge_structure.edge_map, (S64)hovered_object_id);
            if (edge_ptr)
            {
                city::RoadEdge* edge = *edge_ptr;
                osm::WayNode* way_node = osm::way_find(edge->way_id);
                osm::Way* way = &way_node->way;

                bool open = true;
                ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
                for (osm::Tag& tag : way->tags)
                {
                    ImGui::Text("%s: %s", (char*)tag.key.str, (char*)tag.value.str);
                }

                city::RoadInfo* chosen_edge = map_get(ctx->road->road_info_map, edge->id);
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
            osm::WayNode* way_node = osm::way_find(hovered_object_id);
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

        ImGui::Begin("Road Overlays", nullptr);
        ImGui::SetWindowPos(ImVec2(0, 0));

        for (U32 i = 0; i < city::RoadOverlayOption_Count; i++)
        {
            ImGui::RadioButton(city::road_overlay_option_strs[i], (int*)&overlay_option_choice, (int)i);
        }
        ImGui::End();

        // render::blend_3d_draw(ctx->road->handles[ctx->road->current_handle_idx]);
        // render::model_3d_draw(buildings->roof_model_handles);
        // render::model_3d_draw(buildings->facade_model_handles);

        // Update and render Cesium 3D Tiles ////////////
        if (ctx->cesium_tileset)
        {
            cesium::TilesetRenderer* renderer = ctx->cesium_tileset;
            cesium::tileset_update_view(dt_ctx_get()->arena_frame, ctx->cesium_tileset, ctx->camera, framebuffer_dim, ctx->time->delta_time_sec);

            bool overlay_option_changed = overlay_option_choice != ctx->road->overlay_option_cur;
            for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
            {
                if (tile->compute_scheduled == false || overlay_option_changed)
                {
                    tile->compute_scheduled = render::road_intersection_compute_add(tile->render_data.storage_buffer_handle, tile->render_data.index_buffer_handle, road_segment_buffer_handle,
                                                                                    road_segment_node_buffer_handle, road_segment_handle, overlay_option_choice);
                }

                if (tile->compute_scheduled)
                {
                    render::Handle colormap_handle = overlay_option_choice ? ctx->road->colormap_handle : ctx->road->zero_colormap_handle;
                    render::model_3d_draw(tile->render_data, colormap_handle);
                }
            }
            ctx->road->overlay_option_cur = overlay_option_choice;

            /// car simulation rendering
            if (ctx->car_sim)
            {
                Buffer<render::Model3DInstance> instance_buffer = car_sim_update(vk_ctx->draw_frame_arena, ctx->car_sim, ctx->time->delta_time_sec, ctx->cesium_tileset->ecef_to_local);

                // instance buffer offset alignment and assignment
                U32 align = 16;
                render::BufferInfo instance_buffer_info = render::BufferInfo(instance_buffer, render::BufferType_Vertex | render::BufferType_StorageBuffer);
                vulkan::DrawFrame* draw_frame = vk_ctx->draw_frame;
                vulkan::CarInstanceRender* instance_draw = &draw_frame->car_instance_render_list;
                U32 instance_buffer_offset = instance_draw->total_instance_buffer_byte_count + (align - 1);
                instance_buffer_offset -= instance_buffer_offset % align;

                render::car_instance_render_bucket_add(ctx->car_sim->vertex_handle, ctx->car_sim->index_handle, ctx->car_sim->texture_handle, &instance_buffer_info, instance_buffer_offset);

                for (cesium::TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
                {
                    render::car_instance_compute_bucket_add(&instance_buffer_info, tile->render_data.vertex_buffer_handle, tile->render_data.index_buffer_handle, -ctx->car_sim->car_center_offset.min,
                                                            instance_buffer_offset);
                }
            }
        }

        /////////////////////////////////////

        render::render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera, io_ctx->mouse_pos_cur_s64);

        ImGui::EndFrame();
    }
    render::gpu_work_done_wait();

    city::road_destroy(ctx->road);
    city::car_sim_destroy(ctx->car_sim);
    // city::building_destroy(ctx->buildings);
    cesium::tileset_renderer_destroy(ctx->cesium_tileset);
    render::handle_destroy_deferred(road_segment_buffer_handle);
    render::handle_destroy_deferred(road_segment_node_buffer_handle);
    render::handle_destroy_deferred(road_segment_handle);
    render::render_ctx_destroy();
}
