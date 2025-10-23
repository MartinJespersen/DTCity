

static void
TimeInit(DT_Time* time)
{
    time->last_time_ms = os_now_microseconds();
}

static void
UpdateTime(DT_Time* time)
{
    U64 cur_time = os_now_microseconds();
    time->delta_time_sec = (F32)(cur_time - time->last_time_ms) / 1'000'000.0;
    time->last_time_ms = cur_time;
}

static OS_Handle
Entrypoint(Context* ctx, G_Input* input)
{
    ctx->running = 1;
    return OS_ThreadLaunch(MainLoop, input, NULL);
}

static void
Cleanup(OS_Handle thread_handle, Context* ctx)
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
ImguiSetup(VK_Context* vk_ctx, IO* io_ctx)
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

static void
MainLoop(void* ptr)
{
    G_Input* input = (G_Input*)ptr;

    Context* ctx = GlobalContextGet();
    IO* io_ctx = ctx->io;
    OS_SetThreadName(Str8CString("Entrypoint thread"));

    Rng2F32 utm_bb_coords = city::UtmFromBoundingBox(input->bbox);
    printf("UTM: %f %f %f %f\n", utm_bb_coords.min.x, utm_bb_coords.min.y, utm_bb_coords.max.x,
           utm_bb_coords.max.y);
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

    ctx->road = city::RoadCreate(ctx->texture_path, ctx->cache_path, &input->bbox, &sampler_info);
    city::Road* road = ctx->road;

    CameraInit(ctx->camera);
    ctx->buildings = city::BuildingsCreate(ctx->cache_path, ctx->texture_path,
                                           ctx->road->road_height, &input->bbox, &sampler_info);
    city::Buildings* buildings = ctx->buildings;
    ctx->car_sim = city::CarSimCreate(ctx->asset_path, ctx->texture_path, 100, ctx->road);
    city::CarSim* car_sim = ctx->car_sim;

    while (ctx->running)
    {
        UpdateTime(ctx->time);

        IO_NewFrame(io_ctx);
        R_NewFrame();
        ImGui::NewFrame();
        Vec2U32 framebuffer_dim = {.x = (U32)io_ctx->framebuffer_width,
                                   .y = (U32)io_ctx->framebuffer_height};

        bool open = true;
        ImGui::Begin("Test Window", &open, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            ImGui::Text("Window Hovered!");
        }
        else
        {
            CameraUpdate(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);
            ImGui::Text("Currently Hovered Item ID: %llu", R_LatestHoveredObjectIdGet());
            ImVec2 window_size = ImGui::GetWindowSize();
            ImVec2 window_pos = ImVec2((F32)framebuffer_dim.x - window_size.x, 0);
            ImGui::SetWindowPos(window_pos, ImGuiCond_Always);
        }
        ImGui::End();

        VK_Model3DDraw(road->texture_handle, road->vertex_handle, road->index_handle, TRUE, 0,
                       road->index_buffer.size);
        VK_Model3DDraw(buildings->roof_texture_handle, buildings->vertex_handle,
                       buildings->index_handle, FALSE, buildings->roof_index_buffer_offset,
                       buildings->roof_index_count);
        VK_Model3DDraw(buildings->facade_texture_handle, buildings->vertex_handle,
                       buildings->index_handle, FALSE, buildings->facade_index_buffer_offset,
                       buildings->facade_index_count);

        Buffer<city::Model3DInstance> instance_buffer = city::CarUpdate(
            vk_ctx->draw_frame_arena, car_sim, ctx->road, ctx->time->delta_time_sec);
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
