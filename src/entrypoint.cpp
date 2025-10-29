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
    osm_GCSBoundingBox* bbox = &input.bbox;
    F64* bbox_coords[4] = {&bbox->lon_btm_left, &bbox->lat_btm_left, &bbox->lon_top_right,
                           &bbox->lat_top_right};

    if (argc != 1 && argc != 5)
    {
        ExitWithError("G_InterpretInput: Invalid number of arguments");
    }

    if (argc == 1)
    {
        DEBUG_LOG(
            "No command line arguments provided: Using default values\n"
            "lon_btm_left: %lf\n lat_btm_left: %lf\n lon_top_right: %lf\n lat_top_right: %lf\n",
            bbox->lon_btm_left, bbox->lat_btm_left, bbox->lon_top_right, bbox->lat_top_right);

        // Initialize default values
        bbox->lon_btm_left = 9.213970;
        bbox->lat_btm_left = 55.704686;
        bbox->lon_top_right = 9.22868;
        bbox->lat_top_right = 55.713671;
    }

    if (argc == 5)
    {
        for (U32 i = 0; (i < (U64)argc) && (i < ArrayCount(bbox_coords)); ++i)
        {
            char* arg = argv[i + 1];
            String8 arg_str = Str8CString(arg);
            F64 v = F64FromStr8(arg_str);
            *bbox_coords[i] = v;
        }
    }
    return input;
}

static void
dt_main_loop(void* ptr)
{
    dt_Input* input = (dt_Input*)ptr;

    Context* ctx = dt_ctx_get();
    io_IO* io_ctx = ctx->io;
    OS_SetThreadName(Str8CString("Entrypoint thread"));

    Rng2F32 utm_bb_coords = city::UtmFromBoundingBox(input->bbox);
    printf("UTM Coordinates: %f %f %f %f\n", utm_bb_coords.min.x, utm_bb_coords.min.y,
           utm_bb_coords.max.x, utm_bb_coords.max.y);
    R_RenderCtxCreate(ctx->shader_path, io_ctx, ctx->thread_pool);
    VK_Context* vk_ctx = VK_CtxGet();
    ImguiSetup(vk_ctx, io_ctx);

    R_SamplerInfo sampler_info = {
        .min_filter = R_Filter_Linear,
        .mag_filter = R_Filter_Linear,
        .mip_map_mode = R_MipMapMode_Linear,
        .address_mode_u = R_SamplerAddressMode_Repeat,
        .address_mode_v = R_SamplerAddressMode_Repeat,
    };

    U64 node_hashmap_size = 100;
    U64 way_hashmap_size = 100;
    osm_structure_init(node_hashmap_size, way_hashmap_size, &input->bbox);

    ctx->road = city::RoadCreate(ctx->texture_path, ctx->cache_path, &input->bbox, &sampler_info,
                                 osm_g_network);
    city::Road* road = ctx->road;

    ui_camera_init(ctx->camera);
    ctx->buildings =
        city::BuildingsCreate(ctx->cache_path, ctx->texture_path, ctx->road->road_height,
                              &input->bbox, &sampler_info, osm_g_network);
    city::Buildings* buildings = ctx->buildings;
    ctx->car_sim =
        city::CarSimCreate(ctx->asset_path, ctx->texture_path, 100, ctx->road, osm_g_network);
    city::CarSim* car_sim = ctx->car_sim;

    while (ctx->running)
    {
        dt_time_update(ctx->time);

        io_new_frame(io_ctx);
        R_NewFrame();
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
                ImVec2 window_size = ImGui::GetWindowSize();
                ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
                ImGui::SetWindowPos(window_pos, ImGuiCond_Always);
                ImGui::End();
            }
        }

        VK_Model3DDraw(road->texture_handle, road->vertex_handle, road->index_handle, true, 0,
                       road->index_buffer.size);
        VK_Model3DDraw(buildings->roof_texture_handle, buildings->vertex_handle,
                       buildings->index_handle, false, buildings->roof_index_buffer_offset,
                       buildings->roof_index_count);
        VK_Model3DDraw(buildings->facade_texture_handle, buildings->vertex_handle,
                       buildings->index_handle, false, buildings->facade_index_buffer_offset,
                       buildings->facade_index_count);

        Buffer<city::Model3DInstance> instance_buffer =
            city::CarUpdate(vk_ctx->draw_frame_arena, car_sim, ctx->road, ctx->time->delta_time_sec,
                            osm_g_network->utm_node_hashmap);
        R_BufferInfo car_instance_buffer_info =
            R_BufferInfoFromTemplateBuffer(instance_buffer, R_BufferType_Vertex);
        VK_Model3DInstanceDraw(car_sim->texture_handle, car_sim->vertex_handle,
                               car_sim->index_handle, &car_instance_buffer_info);

        R_RenderFrame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera,
                      io_ctx->mouse_pos_cur_s64);
    }
    R_GpuWorkDoneWait();
    city::RoadDestroy(ctx->road);
    city::CarSimDestroy(ctx->car_sim);
    city::BuildingDestroy(ctx->buildings);
    R_RenderCtxDestroy();
}
