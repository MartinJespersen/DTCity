static Buffer<String8>
dt_dir_create(Arena* arena, String8 parent, dt_DataDirPair* dirs, U32 count)
{
    Buffer<String8> buffer = BufferAlloc<String8>(arena, count);
    for (U32 i = 0; i < count; i++)
    {
        String8 dir = Str8PathFromStr8List(arena, {parent, dirs[i].name});
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
    time->delta_time_sec = (F32)(cur_time - time->last_time_ms) / 1'000'000.0;
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
ImguiSetup(VK_Context* vk_ctx, io_IO* io_ctx)
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
        bbox->min.x = 10.1200;
        bbox->min.y = 56.1250;
        bbox->max.x = 10.1330;
        bbox->max.y = 56.1330;
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
    io_IO* io_ctx = ctx->io;
    os_set_thread_name(str8_c_string("Entrypoint thread"));

    Rng2F64 utm_coords = dt_utm_from_wgs84(input->bbox);
    printf("UTM Coordinates: %f %f %f %f\n", utm_coords.min.x, utm_coords.min.y, utm_coords.max.x,
           utm_coords.max.y);
    r_render_ctx_create(ctx->data_subdirs.data[dt_DataDirType::Shaders], io_ctx, ctx->thread_pool);
    VK_Context* vk_ctx = VK_CtxGet();
    ImguiSetup(vk_ctx, io_ctx);

    r_SamplerInfo sampler_info = {
        .min_filter = R_Filter_Linear,
        .mag_filter = R_Filter_Linear,
        .mip_map_mode = R_MipMapMode_Linear,
        .address_mode_u = R_SamplerAddressMode_Repeat,
        .address_mode_v = R_SamplerAddressMode_Repeat,
    };

    U64 node_hashmap_size = 100;
    U64 way_hashmap_size = 100;
    osm_structure_init(node_hashmap_size, way_hashmap_size, utm_coords);

    String8 cache_dir = ctx->data_subdirs.data[dt_DataDirType::Cache];
    String8 texture_dir = ctx->data_subdirs.data[dt_DataDirType::Texture];
    String8 asset_dir = ctx->data_subdirs.data[dt_DataDirType::Assets];

    ctx->road = city_road_create(texture_dir, cache_dir, input->bbox, &sampler_info);
    city_Road* road = ctx->road;

    ui_camera_init(ctx->camera);
    ctx->buildings = city::BuildingsCreate(cache_dir, texture_dir, ctx->road->road_height,
                                           input->bbox, &sampler_info, osm_g_network);
    city::Buildings* buildings = ctx->buildings;
    ctx->car_sim = city::CarSimCreate(asset_dir, texture_dir, 100, ctx->road);
    city::CarSim* car_sim = ctx->car_sim;

    String8 neta_path =
        Str8PathFromStr8List(scratch.arena, {ctx->data_dir, S("netascore.geojson")});
    Map<S64, neta_EdgeList>* edge_map =
        neta_osm_to_edges_map_create(scratch.arena, neta_path, utm_coords);
    if (!edge_map)
    {
        exit_with_error("Failed to initialize neta");
    }

    while (ctx->running)
    {
        dt_time_update(ctx->time);

        io_new_frame(io_ctx);
        r_new_frame();
        ImGui::NewFrame();
        Vec2U32 framebuffer_dim = {.x = (U32)io_ctx->framebuffer_width,
                                   .y = (U32)io_ctx->framebuffer_height};

        {
            ui_camera_update(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);
            U64 hovered_object_id = r_latest_hovered_object_id_get();
            osm_WayNode* way_node = osm_way_find(hovered_object_id);

            if (way_node)
            {
                osm_Way* way = &way_node->way;
                bool open = true;
                ImGui::Begin("Object Info", &open, ImGuiWindowFlags_AlwaysAutoResize);
                for (U32 tag_idx = 0; tag_idx < way->tags.size; tag_idx += 1)
                {
                    osm_Tag* tag = &way->tags.data[tag_idx];
                    ImGui::Text("%s: %s", (char*)tag->key.str, (char*)tag->value.str);
                }

                neta_EdgeList* edge_list = {};
                map_get(edge_map, (S64)hovered_object_id, &edge_list);
                if (edge_list)
                {
                    ImGui::Text("The following are edge coordinates:");

                    for (neta_EdgeNode* edge_node = edge_list->first; edge_node;
                         edge_node = edge_node->next)
                    {
                        if (edge_node->edge->coords.size > 0)
                        {
                            ImGui::Text("Edge %lld:\n", edge_node->edge->edge_id);
                        }
                        for (U32 i = 0; i < edge_node->edge->coords.size; i++)
                        {
                            Vec2F64* coord = edge_node->edge->coords[i];
                            ImGui::Text("(%lf, %lf)", coord->x, coord->y);
                        }
                    }
                }

                ImVec2 window_size = ImGui::GetWindowSize();
                ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
                ImGui::SetWindowPos(window_pos, ImGuiCond_Always);

                ImGui::End();
            }
        }

        r_model_3d_draw(road->handles, true);
        r_model_3d_draw(buildings->roof_model_handles, false);
        r_model_3d_draw(buildings->facade_model_handles, false);

        Buffer<r_Model3DInstance> instance_buffer =
            CarUpdate(vk_ctx->draw_frame_arena, car_sim, ctx->time->delta_time_sec);
        r_BufferInfo car_instance_buffer_info =
            r_buffer_info_from_template_buffer(instance_buffer, R_BufferType_Vertex);
        VK_Model3DInstanceDraw(car_sim->texture_handle, car_sim->vertex_handle,
                               car_sim->index_handle, &car_instance_buffer_info);

        r_render_frame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera,
                       io_ctx->mouse_pos_cur_s64);
    }
    r_gpu_work_done_wait();

    city::road_destroy(ctx->road);
    city::car_sim_destroy(ctx->car_sim);
    city::building_destroy(ctx->buildings);
    r_render_ctx_destroy();
}
