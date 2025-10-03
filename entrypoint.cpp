

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
Entrypoint(Context* ctx)
{
    ctx->running = 1;
    return OS_ThreadLaunch(MainLoop, ctx, NULL);
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
ImguiSetup(wrapper::VulkanContext* vk_ctx, IO* io_ctx)
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
    init_info.PipelineInfoMain.MSAASamples = vk_ctx->msaa_samples;
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.UseDynamicRendering = VK_TRUE;
    ImGui_ImplVulkan_Init(&init_info);
}

static void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    IO* io_ctx = ctx->io;
    OS_SetThreadName(Str8CString("Entrypoint thread"));
    city::GCSBoundingBox gcs_bbox = {.lat_btm_left = 56.16923976826141,
                                     .lon_btm_left = 10.1852768812041,
                                     .lat_top_right = 56.17371342689877,
                                     .lon_top_right = 10.198376789774187};

    R_RenderCtxCreate(ctx->shader_path, io_ctx, ctx->thread_pool);
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    ImguiSetup(vk_ctx, io_ctx);

    ctx->road = city::RoadCreate(ctx->texture_path, ctx->cache_path, &gcs_bbox);
    city::Road* road = ctx->road;
    ctx->buildings = city::BuildingsCreate(ctx->cache_path, ctx->texture_path,
                                           ctx->road->road_height, &gcs_bbox);

    CameraInit(ctx->camera);
    ctx->car_sim = city::CarSimCreate(ctx->asset_path, ctx->texture_path, 100, ctx->road);
    city::CarSim* car_sim = ctx->car_sim;

    R_SamplerInfo sampler_info = {
        .min_filter = R_Filter_Linear,
        .mag_filter = R_Filter_Linear,
        .mip_map_mode = R_MipMapMode_Linear,
        .address_mode_u = R_SamplerAddressMode_Repeat,
        .address_mode_v = R_SamplerAddressMode_Repeat,
    };

    city::Buildings* buildings = ctx->buildings;

    // buildings buffer create
    R_BufferInfo building_vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(buildings->vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo building_index_buffer_info =
        R_BufferInfoFromTemplateBuffer(buildings->index_buffer, R_BufferType_Index);

    // road buffers create
    R_BufferInfo road_vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(road->vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo road_index_buffer_info =
        R_BufferInfoFromTemplateBuffer(road->index_buffer, R_BufferType_Index);

    // car buffers create
    R_BufferInfo car_vertex_buffer_info =
        R_BufferInfoFromTemplateBuffer(car_sim->vertex_buffer, R_BufferType_Vertex);
    R_BufferInfo car_index_buffer_info =
        R_BufferInfoFromTemplateBuffer(car_sim->index_buffer, R_BufferType_Index);

    while (ctx->running)
    {
        IO_NewFrame(io_ctx);
        R_NewFrame();
        ImGui::NewFrame();
        Vec2U32 framebuffer_dim = {.x = (U32)io_ctx->framebuffer_width,
                                   .y = (U32)io_ctx->framebuffer_height};
        UpdateTime(ctx->time);
        CameraUpdate(ctx->camera, ctx->io, ctx->time->delta_time_sec, framebuffer_dim);
        // ~mgj: Test UI
        bool show_demo_window = TRUE;
        ImGui::ShowDemoWindow(&show_demo_window);

        Buffer<city::Model3DInstance> instance_buffer = city::CarUpdate(
            vk_ctx->draw_frame_arena, car_sim, ctx->road, ctx->time->delta_time_sec);
        R_BufferInfo car_instance_buffer_info =
            R_BufferInfoFromTemplateBuffer(instance_buffer, R_BufferType_Vertex);
        wrapper::Model3DDraw(&road->asset_vertex_info, &road->asset_index_info,
                             &road->asset_texture_info, road->texture_path, &sampler_info,
                             &road_vertex_buffer_info, &road_index_buffer_info, TRUE, 0,
                             road->index_buffer.size);

        wrapper::Model3DDraw(&buildings->vertex_buffer_info, &buildings->index_buffer_info,
                             &buildings->roof_texture_info, buildings->roof_texture_path,
                             &sampler_info, &building_vertex_buffer_info,
                             &building_index_buffer_info, FALSE,
                             buildings->roof_index_buffer_offset, buildings->roof_index_count);
        wrapper::Model3DDraw(&buildings->vertex_buffer_info, &buildings->index_buffer_info,
                             &buildings->facade_texture_info, buildings->facade_texture_path,
                             &sampler_info, &building_vertex_buffer_info,
                             &building_index_buffer_info, FALSE,
                             buildings->facade_index_buffer_offset, buildings->facade_index_count);
        wrapper::Model3DInstanceDraw(&car_sim->vertex_buffer_info, &car_sim->index_buffer_info,
                                     &car_sim->texture_info, car_sim->texture_path, &sampler_info,
                                     &car_vertex_buffer_info, &car_index_buffer_info,
                                     &car_instance_buffer_info);

        R_RenderFrame(framebuffer_dim, &io_ctx->framebuffer_resized, ctx->camera,
                      io_ctx->mouse_pos_cur_s64);
    }
    R_GpuWorkDoneWait();
    city::RoadDestroy(ctx->road);
    city::CarSimDestroy(ctx->car_sim);
    city::BuildingDestroy(ctx->buildings);
    R_RenderCtxDestroy();
}
