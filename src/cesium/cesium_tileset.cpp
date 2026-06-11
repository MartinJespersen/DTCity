namespace cesium
{

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task Processor Implementation
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class DTCityTaskProcessor : public CesiumAsync::ITaskProcessor
{
  public:
    DTCityTaskProcessor(async::ThreadPool* thread_pool) : thread_pool(thread_pool)
    {
    }

    void
    startTask(std::function<void()> task) override
    {
        prof_scope_marker;
        // Create a copy of the task on the heap
        auto* task_copy = new std::function<void()>(std::move(task));

        async::WorkerItem item = async::WorkerItem(task_copy,
                                                   [](async::ThreadInfo, async::WorkerData data) -> async::WorkerResult
                                                   {
                                                       // ~mgj: Enqueue the command buffer
                                                       auto* func = static_cast<std::function<void()>*>(data);
                                                       (*func)();
                                                       delete func;
                                                       return {};
                                                   });

        if (!async::thread_pool_push(thread_pool, &item))
        {
            exit_with_error("Failed to enqueue task");
            (*task_copy)();
            delete task_copy;
        }
    }

  private:
    async::ThreadPool* thread_pool;
};

g_internal render::SamplerInfo
_raster_sampler_info_get(const std::any& renderer_options)
{
    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_ClampToEdge,
        .address_mode_v = render::SamplerAddressMode_ClampToEdge,
        .unnormalized_coordinates = false,
    };

    if (const render::SamplerInfo* opt = std::any_cast<render::SamplerInfo>(&renderer_options))
    {
        sampler_info = *opt;
    }

    return sampler_info;
}

g_internal bool
_raster_texture_upload_data_prepare(Arena* arena, const CesiumGltf::ImageAsset& image, render::TextureUploadData* out_texture_upload)
{
    if (!out_texture_upload)
    {
        return false;
    }

    if (image.width <= 0 || image.height <= 0 || image.channels <= 0 || image.pixelData.empty())
    {
        return false;
    }

    if (image.bytesPerChannel != 1)
    {
        DEBUG_LOG("Unsupported Cesium raster image format: %d bytes per channel", image.bytesPerChannel);
        return false;
    }

    U64 pixel_count_u64 = (U64)image.width * (U64)image.height;
    U64 src_byte_count = pixel_count_u64 * (U64)image.channels;
    if ((U64)image.pixelData.size() < src_byte_count)
    {
        DEBUG_LOG("Cesium raster image buffer is smaller than expected");
        return false;
    }

    if (pixel_count_u64 > (U64)(max_U32 / 4))
    {
        DEBUG_LOG("Cesium raster image is too large to upload");
        return false;
    }

    const U8* src = (const U8*)image.pixelData.data();
    U32 pixel_count = safe_cast_u32(pixel_count_u64);
    U32 dst_byte_count = safe_cast_u32(pixel_count_u64 * 4);
    U8* dst = PushArray(arena, U8, dst_byte_count);

    switch (image.channels)
    {
        case 1:
        {
            for (U32 i = 0; i < pixel_count; ++i)
            {
                U8 value = src[i];
                dst[i * 4 + 0] = value;
                dst[i * 4 + 1] = value;
                dst[i * 4 + 2] = value;
                dst[i * 4 + 3] = 255;
            }
        }
        break;

        case 2:
        {
            for (U32 i = 0; i < pixel_count; ++i)
            {
                U8 value = src[i * 2 + 0];
                dst[i * 4 + 0] = value;
                dst[i * 4 + 1] = value;
                dst[i * 4 + 2] = value;
                dst[i * 4 + 3] = src[i * 2 + 1];
            }
        }
        break;

        case 3:
        {
            for (U32 i = 0; i < pixel_count; ++i)
            {
                dst[i * 4 + 0] = src[i * 3 + 0];
                dst[i * 4 + 1] = src[i * 3 + 1];
                dst[i * 4 + 2] = src[i * 3 + 2];
                dst[i * 4 + 3] = 255;
            }
        }
        break;

        case 4:
        {
            MemoryCopy(dst, src, dst_byte_count);
        }
        break;

        default:
        {
            DEBUG_LOG("Unsupported Cesium raster image channel count: %d", image.channels);
            return false;
        }
    }

    *out_texture_upload = render::TextureUploadData::init(dst, (U32)image.width, (U32)image.height, 4, 1, dst_byte_count);
    return true;
}

g_internal TileRenderDataList*
_tile_render_data_list_get(const Cesium3DTilesSelection::Tile& tile)
{
    const Cesium3DTilesSelection::TileContent& content = tile.getContent();
    const Cesium3DTilesSelection::TileRenderContent* render_content = content.getRenderContent();
    if (!render_content)
    {
        return nullptr;
    }

    return static_cast<TileRenderDataList*>(render_content->getRenderResources());
}

g_internal TileRasterOverlayAttachment*
_tile_raster_attachment_find(TileRenderDataList* render_data_list, const CesiumRasterOverlays::RasterOverlayTile& raster_tile, S32 overlay_texture_coordinate_id)
{
    if (!render_data_list)
    {
        return nullptr;
    }

    for (TileRasterOverlayAttachment* attachment = render_data_list->raster_overlay_first; attachment; attachment = attachment->next)
    {
        if (attachment->raster_tile == &raster_tile && attachment->overlay_texture_coordinate_id == overlay_texture_coordinate_id)
        {
            return attachment;
        }
    }

    return nullptr;
}

g_internal TileRasterOverlayAttachment*
_tile_active_raster_attachment_get(TileRenderDataList* render_data_list)
{
    if (!render_data_list)
    {
        return nullptr;
    }

    TileRasterOverlayAttachment* result = nullptr;

    for (TileRasterOverlayAttachment* attachment = render_data_list->raster_overlay_first; attachment; attachment = attachment->next)
    {
        if (attachment->overlay_texture_coordinate_id == 0 && render::is_handle_zero(attachment->texture_handle) == false)
        {
            result = attachment;
            break;
        }
    }

    return result;
}

g_internal void
_tile_render_data_overlay_apply(TileRenderDataList* render_data_list)
{
    TileRasterOverlayAttachment* attachment = _tile_active_raster_attachment_get(render_data_list);

    for (TileRenderData* render_data = render_data_list ? render_data_list->first : nullptr; render_data; render_data = render_data->next)
    {
        render_data->render_data.overlay_texture_handle = {};
        render_data->render_data.overlay_translation = {};
        render_data->render_data.overlay_scale = {1.0f, 1.0f};
        render_data->render_data.overlay_texture_coordinate_id = -1;

        if (attachment)
        {
            render_data->render_data.overlay_texture_handle = attachment->texture_handle;
            render_data->render_data.overlay_translation.x = (F32)attachment->translation.x;
            // Cesium overlay V is south-to-north. Uploaded raster rows are north-to-south.
            render_data->render_data.overlay_translation.y = (F32)(1.0 - attachment->translation.y);
            render_data->render_data.overlay_scale.x = (F32)attachment->scale.x;
            render_data->render_data.overlay_scale.y = (F32)(-attachment->scale.y);
            render_data->render_data.overlay_texture_coordinate_id = attachment->overlay_texture_coordinate_id;
        }
    }
}

g_internal std::string
_std_string_from_str8(String8 string)
{
    return std::string((const char*)string.str, (size_t)string.size);
}

g_internal void
_ion_raster_overlay_example_add_if_present(Cesium3DTilesSelection::Tileset* tileset)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 ion_access_token = {};
    if (env_vars_value_get(scratch.arena, S("CESIUM_ION_ACCESS_TOKEN"), &ion_access_token, 1))
    {
        return;
    }

    String8 ion_raster_asset_id_str = {};
    if (env_vars_value_get(scratch.arena, S("CESIUM_ION_RASTER_OVERLAY_ASSET_ID"), &ion_raster_asset_id_str, 1))
    {
        INFO_LOG("Cesium Ion raster overlay example disabled. Set CESIUM_ION_RASTER_OVERLAY_ASSET_ID to enable it.");
        return;
    }

    S64 ion_raster_asset_id = s64_from_str8(ion_raster_asset_id_str, 10);
    if (ion_raster_asset_id <= 0)
    {
        ERROR_LOG("Invalid CESIUM_ION_RASTER_OVERLAY_ASSET_ID value: %.*s\n", str8_varg(ion_raster_asset_id_str));
        return;
    }

    String8 overlay_name_str = {};
    std::string overlay_name = "cesium_ion_raster_overlay";
    if (!env_vars_value_get(scratch.arena, S("CESIUM_ION_RASTER_OVERLAY_NAME"), &overlay_name_str, 1))
    {
        overlay_name = _std_string_from_str8(overlay_name_str);
    }

    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_ClampToEdge,
        .address_mode_v = render::SamplerAddressMode_ClampToEdge,
        .unnormalized_coordinates = false,
    };

    CesiumRasterOverlays::RasterOverlayOptions overlay_options = {};
    overlay_options.maximumSimultaneousTileLoads = 20;
    overlay_options.maximumTextureSize = 2048;
    overlay_options.maximumScreenSpaceError = 2.0;
    overlay_options.rendererOptions = sampler_info;
    overlay_options.loadErrorCallback = [](const CesiumRasterOverlays::RasterOverlayLoadFailureDetails& details)
    {
        const char* load_type = "Unknown";
        switch (details.type)
        {
            case CesiumRasterOverlays::RasterOverlayLoadType::CesiumIon: load_type = "CesiumIon"; break;
            case CesiumRasterOverlays::RasterOverlayLoadType::TileProvider: load_type = "TileProvider"; break;
            case CesiumRasterOverlays::RasterOverlayLoadType::Unknown: break;
            default: break;
        }

        ERROR_LOG("Cesium ion raster overlay load error: type=%s message=%s\n", load_type, details.message.c_str());
    };

    CesiumUtility::IntrusivePointer<const CesiumRasterOverlays::RasterOverlay> ion_overlay =
        new CesiumRasterOverlays::IonRasterOverlay(overlay_name, ion_raster_asset_id, _std_string_from_str8(ion_access_token), overlay_options);

    tileset->getOverlays().add(ion_overlay);
    INFO_LOG("Added Cesium ion raster overlay example: %s (asset_id=%lld)", overlay_name.c_str(), ion_raster_asset_id);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Prepare Renderer Resources Implementation
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class DTCityPrepareRendererResources : public Cesium3DTilesSelection::IPrepareRendererResources
{
  public:
    DTCityPrepareRendererResources(glm::dmat4 ecef_to_local_transform, TilesetRenderer* tile_set_renderer) : ecef_to_local_transform(ecef_to_local_transform), tile_set_renderer(tile_set_renderer)
    {
    }

    CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources>
    prepareInLoadThread(const CesiumAsync::AsyncSystem& asyncSystem, Cesium3DTilesSelection::TileLoadResult&& tileLoadResult, const glm::dmat4& transform, const std::any& rendererOptions) override
    {
        CesiumGltf::Model* model = std::get_if<CesiumGltf::Model>(&tileLoadResult.contentKind);

        if (!model)
        {
            return asyncSystem.createResolvedFuture(Cesium3DTilesSelection::TileLoadResultAndRenderResources{std::move(tileLoadResult), nullptr});
        }

        render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
        render::thread_cmd_buffer_record(thread_ctx);
        defer({ render::thread_cmd_buffer_end(thread_ctx); });

        TileRenderDataList* render_data_list = tile_render_data_from_gltf(*model, ecef_to_local_transform, transform, tileLoadResult.glTFUpAxis, thread_ctx);
        {
            auto stub_func = [](void* data, render::ThreadWorkerCmdCtx* thread_input)
            {
                (void)data;
                (void)thread_input;
            };
            B32 is_map_tile = false;
            const bool* is_map_tile_ptr = std::any_cast<bool>(&rendererOptions);
            if (is_map_tile_ptr)
            {
                is_map_tile = *is_map_tile_ptr;
            }
            for (TileRenderData* data = render_data_list->first; data; data = data->next)
            {
                data->render_data.is_map_tile = is_map_tile;
                render::handle_list_push(thread_ctx, data->render_data.vertex_buffer_handle);
                render::handle_list_push(thread_ctx, data->render_data.index_buffer_handle);
                render::Handle null_texture_handle = render::texture_zero_handle_get();
                if (data->render_data.texture_handle.u64 != null_texture_handle.u64 || data->render_data.texture_handle.gen_id != null_texture_handle.gen_id ||
                    data->render_data.texture_handle.type != null_texture_handle.type)
                {
                    render::handle_list_push(thread_ctx, data->render_data.texture_handle);
                }

                thread_ctx->loading_func = stub_func;
            }
        }
        return asyncSystem.createResolvedFuture(Cesium3DTilesSelection::TileLoadResultAndRenderResources{std::move(tileLoadResult), render_data_list});
    }

    void*
    prepareInMainThread(Cesium3DTilesSelection::Tile& tile, void* pLoadThreadResult) override
    {
        (void)tile;
        TileRenderDataList* render_data_list = static_cast<TileRenderDataList*>(pLoadThreadResult);
        render_data_list->tile_is_loaded = true;
        return render_data_list;
    }

    void
    free(Cesium3DTilesSelection::Tile& tile, void* pLoadThreadResult, void* pMainThreadResult) noexcept override
    {
        prof_scope_marker;
        (void)tile;
        (void)pLoadThreadResult;

        TileRenderDataList* render_data_list = static_cast<TileRenderDataList*>(pMainThreadResult ? pMainThreadResult : pLoadThreadResult);

        if (!render_data_list)
            return;

        OS_MutexScopeW(tile_set_renderer->tiles_to_free_mutex)
        {
            SLLStackPush(tile_set_renderer->tiles_to_free_stack, render_data_list);
            tile_set_renderer->tiles_to_free_stack_count++;
        }
    }

    void*
    prepareRasterInLoadThread(CesiumGltf::ImageAsset& image, const std::any& rendererOptions) override
    {
        prof_scope_marker;

        RasterTileInfo tile_info(image, rendererOptions);
        render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
        render::thread_cmd_buffer_record(thread_ctx);
        defer({ render::thread_cmd_buffer_end(thread_ctx); });
        render::BBoxDraw* raster_tile = render_raster_tile_record(thread_ctx, &tile_info);

        return raster_tile;
    }

    void*
    prepareRasterInMainThread(CesiumRasterOverlays::RasterOverlayTile& rasterTile, void* pLoadThreadResult) override
    {
        (void)rasterTile;
        render::BBoxDraw* load_result = static_cast<render::BBoxDraw*>(pLoadThreadResult);
        if (!load_result)
        {
            return nullptr;
        }

        return load_result;
    }

    void
    freeRaster(const CesiumRasterOverlays::RasterOverlayTile& rasterTile, void* pLoadThreadResult, void* pMainThreadResult) noexcept override
    {
        (void)rasterTile;
        render::BBoxDraw* result = static_cast<render::BBoxDraw*>(pMainThreadResult ? pMainThreadResult : pLoadThreadResult);
        if (result)
        {
            Assert(render::is_handle_zero(result->tex) == false);
            render::handle_destroy_deferred(result->tex);
            arena_release(result->arena);
        }
    }

    void
    attachRasterInMainThread(const Cesium3DTilesSelection::Tile& tile, int32_t overlayTextureCoordinateID, const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
                             void* pMainThreadRendererResources, const glm::dvec2& translation, const glm::dvec2& scale) override
    {
        TileRenderDataList* render_data_list = _tile_render_data_list_get(tile);
        render::BBoxDraw* raster_resources = static_cast<render::BBoxDraw*>(pMainThreadRendererResources);
        if (!render_data_list || !render_data_list->arena || !raster_resources)
        {
            return;
        }

        TileRasterOverlayAttachment* attachment = _tile_raster_attachment_find(render_data_list, rasterTile, overlayTextureCoordinateID);
        if (!attachment)
        {
            attachment = PushStruct(render_data_list->arena, TileRasterOverlayAttachment);
            SLLStackPush(render_data_list->raster_overlay_first, attachment);
        }

        attachment->raster_tile = &rasterTile;
        attachment->raster_renderer_resources = pMainThreadRendererResources;
        attachment->texture_handle = raster_resources->tex;
        attachment->overlay_texture_coordinate_id = overlayTextureCoordinateID;
        attachment->translation = translation;
        attachment->scale = scale;
    }

    void
    detachRasterInMainThread(const Cesium3DTilesSelection::Tile& tile, int32_t overlayTextureCoordinateID, const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
                             void* pMainThreadRendererResources) noexcept override
    {
        TileRenderDataList* render_data_list = _tile_render_data_list_get(tile);
        if (!render_data_list)
        {
            return;
        }

        TileRasterOverlayAttachment** prev_next = &render_data_list->raster_overlay_first;
        for (TileRasterOverlayAttachment* attachment = render_data_list->raster_overlay_first; attachment; attachment = attachment->next)
        {
            bool same_raster = attachment->raster_tile == &rasterTile;
            if (!same_raster && pMainThreadRendererResources)
            {
                same_raster = attachment->raster_renderer_resources == pMainThreadRendererResources;
            }

            if (same_raster && attachment->overlay_texture_coordinate_id == overlayTextureCoordinateID)
            {
                *prev_next = attachment->next;
                break;
            }

            prev_next = &attachment->next;
        }
    }

  private:
    glm::dmat4 ecef_to_local_transform;
    TilesetRenderer* tile_set_renderer;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper: Convert CesiumGltf::Sampler to render::SamplerInfo
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal render::SamplerInfo
sampler_info_from_cesium_sampler(const CesiumGltf::Sampler& sampler)
{
    render::Filter min_filter = render::Filter_Linear;
    render::Filter mag_filter = render::Filter_Linear;
    render::MipMapMode mipmap_mode = render::MipMapMode_Linear;
    render::SamplerAddressMode address_mode_u = render::SamplerAddressMode_Repeat;
    render::SamplerAddressMode address_mode_v = render::SamplerAddressMode_Repeat;

    if (sampler.minFilter.has_value())
    {
        switch (sampler.minFilter.value())
        {
            case CesiumGltf::Sampler::MinFilter::NEAREST:
                min_filter = render::Filter_Nearest;
                mipmap_mode = render::MipMapMode_Nearest;
                break;
            case CesiumGltf::Sampler::MinFilter::LINEAR:
                min_filter = render::Filter_Linear;
                mipmap_mode = render::MipMapMode_Nearest;
                break;
            case CesiumGltf::Sampler::MinFilter::NEAREST_MIPMAP_NEAREST:
                min_filter = render::Filter_Nearest;
                mipmap_mode = render::MipMapMode_Nearest;
                break;
            case CesiumGltf::Sampler::MinFilter::LINEAR_MIPMAP_NEAREST:
                min_filter = render::Filter_Linear;
                mipmap_mode = render::MipMapMode_Nearest;
                break;
            case CesiumGltf::Sampler::MinFilter::NEAREST_MIPMAP_LINEAR:
                min_filter = render::Filter_Nearest;
                mipmap_mode = render::MipMapMode_Linear;
                break;
            case CesiumGltf::Sampler::MinFilter::LINEAR_MIPMAP_LINEAR:
                min_filter = render::Filter_Linear;
                mipmap_mode = render::MipMapMode_Linear;
                break;
            default: InvalidPath; break;
        }
    }

    if (sampler.magFilter.has_value())
    {
        switch (sampler.magFilter.value())
        {
            case CesiumGltf::Sampler::MagFilter::NEAREST: mag_filter = render::Filter_Nearest; break;
            case CesiumGltf::Sampler::MagFilter::LINEAR: mag_filter = render::Filter_Linear; break;
            default: InvalidPath; break;
        }
    }

    switch (sampler.wrapS)
    {
        case CesiumGltf::Sampler::WrapS::CLAMP_TO_EDGE: address_mode_u = render::SamplerAddressMode_ClampToEdge; break;
        case CesiumGltf::Sampler::WrapS::MIRRORED_REPEAT: address_mode_u = render::SamplerAddressMode_MirroredRepeat; break;
        case CesiumGltf::Sampler::WrapS::REPEAT: address_mode_u = render::SamplerAddressMode_Repeat; break;
        default: InvalidPath; break;
    }

    switch (sampler.wrapT)
    {
        case CesiumGltf::Sampler::WrapT::CLAMP_TO_EDGE: address_mode_v = render::SamplerAddressMode_ClampToEdge; break;
        case CesiumGltf::Sampler::WrapT::MIRRORED_REPEAT: address_mode_v = render::SamplerAddressMode_MirroredRepeat; break;
        case CesiumGltf::Sampler::WrapT::REPEAT: address_mode_v = render::SamplerAddressMode_Repeat; break;
        default: InvalidPath; break;
    }

    render::SamplerInfo sampler_info = {.min_filter = min_filter, .mag_filter = mag_filter, .mip_map_mode = mipmap_mode, .address_mode_u = address_mode_u, .address_mode_v = address_mode_v};
    return sampler_info;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper: Convert Cesium glTF to render data
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal render::BBoxDraw*
render_raster_tile_record(render::ThreadWorkerCmdCtx* thread_input, RasterTileInfo* tile_info)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* raster_arena = arena_alloc();
    render::BBoxDraw* result = PushStruct(raster_arena, render::BBoxDraw);
    result->arena = raster_arena;

    render::SamplerInfo sampler_info = _raster_sampler_info_get(tile_info->renderer_options);
    render::TextureUploadData texture_upload = {};
    if (!_raster_texture_upload_data_prepare(scratch.arena, tile_info->image, &texture_upload))
    {
        arena_release(raster_arena);
        return nullptr;
    }
    result->tex = render::texture_load_sync(&sampler_info, &texture_upload, thread_input->cmd_buffer);
    {
        auto stub_func = [](void* data, render::ThreadWorkerCmdCtx* thread_input)
        {
            (void)data;
            (void)thread_input;
        };

        render::handle_list_push(thread_input, result->tex);
        thread_input->loading_func = stub_func;
    }
    return result;
}

g_internal TileRenderDataList*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local, const glm::dmat4& tile_transform, CesiumGeometry::Axis gltf_up_axis,
                           render::ThreadWorkerCmdCtx* thread_input)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    Arena* tile_arena = arena_alloc();
    TileRenderDataList* tile_render_data_list = PushStruct(tile_arena, TileRenderDataList);
    tile_render_data_list->arena = tile_arena;
    glm::dmat4 gltf_to_zup = CesiumGeometry::Transforms::getUpAxisTransform(gltf_up_axis, CesiumGeometry::Axis::Z);

    struct PrimitiveNode
    {
        PrimitiveNode* next;
        Buffer<render::TileVertex> vertices;
        Buffer<U32> indices;
        U32 material_idx;
        B32 has_overlay_uv;
    };

    struct PrimitiveList
    {
        PrimitiveNode* first;
        PrimitiveNode* last;
    };

    U32 model_scene_count = model.scenes.size();
    Assert(model_scene_count == 1);
    for (U32 i = 0; i < model.nodes.size(); i++)
    {
        Assert(model.nodes[i].children.size() == 0);
    }
    PrimitiveList primitive_list = {};
    for (const CesiumGltf::Node& node : model.nodes)
    {
        for (const CesiumGltf::Mesh& mesh : model.meshes)
        {
            for (const CesiumGltf::MeshPrimitive& primitive : mesh.primitives)
            {
                auto pos_it = primitive.attributes.find("POSITION");
                if (pos_it == primitive.attributes.end())
                    continue;

                const CesiumGltf::Accessor* pos_accessor = CesiumGltf::Model::getSafe(&model.accessors, pos_it->second);
                if (!pos_accessor)
                    continue;

                // Get UV accessor if available
                const CesiumGltf::Accessor* uv_accessor = nullptr;
                auto uv_it = primitive.attributes.find("TEXCOORD_0");
                if (uv_it != primitive.attributes.end())
                {
                    uv_accessor = CesiumGltf::Model::getSafe(&model.accessors, uv_it->second);
                }

                const CesiumGltf::Accessor* overlay_uv_accessor = nullptr;
                auto overlay_uv_it = primitive.attributes.find("_CESIUMOVERLAY_0");
                if (overlay_uv_it != primitive.attributes.end())
                {
                    overlay_uv_accessor = CesiumGltf::Model::getSafe(&model.accessors, overlay_uv_it->second);
                }

                // Get buffer views and buffers for position
                const CesiumGltf::BufferView* pos_buffer_view = CesiumGltf::Model::getSafe(&model.bufferViews, pos_accessor->bufferView);
                if (!pos_buffer_view)
                    continue;

                const CesiumGltf::Buffer* pos_buffer = CesiumGltf::Model::getSafe(&model.buffers, pos_buffer_view->buffer);
                if (!pos_buffer)
                    continue;

                // Read positions
                const U8* pos_data = (U8*)pos_buffer->cesium.data.data() + pos_buffer_view->byteOffset + pos_accessor->byteOffset;

                S64 pos_stride = pos_buffer_view->byteStride.value_or(0);
                if (pos_stride == 0)
                {
                    pos_stride = sizeof(F32) * 3;
                }
                // Assert(pos_stride <= (S64)(sizeof(F32) * 3));

                // Get UV data if available
                const U8* uv_data = nullptr;
                U32 uv_stride = 0;
                if (uv_accessor)
                {
                    const CesiumGltf::BufferView* uv_buffer_view = CesiumGltf::Model::getSafe(&model.bufferViews, uv_accessor->bufferView);
                    if (uv_buffer_view)
                    {
                        const CesiumGltf::Buffer* uv_buffer = CesiumGltf::Model::getSafe(&model.buffers, uv_buffer_view->buffer);
                        if (uv_buffer)
                        {
                            uv_data = (U8*)uv_buffer->cesium.data.data() + uv_buffer_view->byteOffset + uv_accessor->byteOffset;
                            U32 uv_byte_stride = uv_buffer_view->byteStride.value_or(0);
                            uv_stride = uv_byte_stride > 0 ? uv_byte_stride : sizeof(F32) * 2;
                        }
                    }
                }

                const U8* overlay_uv_data = nullptr;
                U32 overlay_uv_stride = 0;
                if (overlay_uv_accessor)
                {
                    const CesiumGltf::BufferView* overlay_uv_buffer_view = CesiumGltf::Model::getSafe(&model.bufferViews, overlay_uv_accessor->bufferView);
                    if (overlay_uv_buffer_view)
                    {
                        const CesiumGltf::Buffer* overlay_uv_buffer = CesiumGltf::Model::getSafe(&model.buffers, overlay_uv_buffer_view->buffer);
                        if (overlay_uv_buffer)
                        {
                            overlay_uv_data = (U8*)overlay_uv_buffer->cesium.data.data() + overlay_uv_buffer_view->byteOffset + overlay_uv_accessor->byteOffset;
                            U32 overlay_uv_byte_stride = overlay_uv_buffer_view->byteStride.value_or(0);
                            overlay_uv_stride = overlay_uv_byte_stride > 0 ? overlay_uv_byte_stride : sizeof(F32) * 2;
                        }
                    }
                }

                const CesiumGltf::Accessor* index_accessor = CesiumGltf::Model::getSafe(&model.accessors, primitive.indices);

                // Allocate buffers
                Buffer<render::TileVertex> vertices = buffer_alloc<render::TileVertex>(scratch.arena, pos_accessor->count);
                Buffer<U32> indices = buffer_alloc<U32>(scratch.arena, index_accessor->count);

                // Copy vertices
                for (U32 i = 0; i < (U32)pos_accessor->count; ++i)
                {
                    render::TileVertex* vertex = &vertices.data[i];
                    vertex->colormap_value = 0.0f;
                    vertex->uv = {};
                    vertex->overlay_uv = {};
                    vertex->object_id = {};

                    const F32* pos = (const F32*)(pos_data + i * pos_stride);
                    glm::dmat4 node_matrix(glm::dvec4(node.matrix[0], node.matrix[1], node.matrix[2], node.matrix[3]), glm::dvec4(node.matrix[4], node.matrix[5], node.matrix[6], node.matrix[7]),
                                           glm::dvec4(node.matrix[8], node.matrix[9], node.matrix[10], node.matrix[11]),
                                           glm::dvec4(node.matrix[12], node.matrix[13], node.matrix[14], node.matrix[15]));
                    glm::dvec4 pos_node = node_matrix * glm::dvec4(pos[0], pos[1], pos[2], 1.0);
                    glm::dvec4 pos_local = ecef_to_local * tile_transform * gltf_to_zup * pos_node;

                    vertex->pos.x = (F32)pos_local.x;
                    vertex->pos.y = (F32)pos_local.y;
                    vertex->pos.z = (F32)pos_local.z;

                    if (uv_data)
                    {
                        const F32* uv = (const F32*)(uv_data + (U64)i * (U64)uv_stride);
                        vertex->uv.x = uv[0];
                        vertex->uv.y = uv[1];
                    }

                    if (overlay_uv_data)
                    {
                        const F32* overlay_uv = (const F32*)(overlay_uv_data + (U64)i * (U64)overlay_uv_stride);
                        vertex->overlay_uv.x = overlay_uv[0];
                        vertex->overlay_uv.y = overlay_uv[1];
                    }
                }

                // Read indices
                if (primitive.indices >= 0)
                {
                    if (index_accessor)
                    {
                        const CesiumGltf::BufferView* index_buffer_view = CesiumGltf::Model::getSafe(&model.bufferViews, index_accessor->bufferView);
                        if (index_buffer_view)
                        {
                            const CesiumGltf::Buffer* index_buffer = CesiumGltf::Model::getSafe(&model.buffers, index_buffer_view->buffer);
                            if (index_buffer)
                            {
                                U8* index_data = (U8*)index_buffer->cesium.data.data() + index_buffer_view->byteOffset + index_accessor->byteOffset;

                                U32 stride = index_accessor->componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE    ? sizeof(U8)
                                             : index_accessor->componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT ? sizeof(U16)
                                                                                                                                    : sizeof(U32);

                                for (U32 i = 0; i < (U32)index_accessor->count; ++i)
                                {
                                    U8* base = index_data + (U64)i * stride;
                                    MemoryCopy(indices.data + i, base, stride);
                                }
                            }
                        }
                    }
                }

                PrimitiveNode* primitive_node = PushStruct(scratch.arena, PrimitiveNode);
                primitive_node->vertices = vertices;
                primitive_node->indices = indices;
                primitive_node->material_idx = primitive.material;
                primitive_node->has_overlay_uv = overlay_uv_data != nullptr;

                SLLQueuePush(primitive_list.first, primitive_list.last, primitive_node);
            }
        }
    }
    U64 mat_count = model.materials.size();
    for (U32 mat_idx = 0; mat_idx < mat_count; ++mat_idx)
    {
        TileRenderData* render_data = PushStruct(tile_render_data_list->arena, TileRenderData);
        SLLQueuePush(tile_render_data_list->first, tile_render_data_list->last, render_data);

        // Create and async load index and vertex buffer
        // First pass: count vertices/indices only for primitives with this material
        U32 vertex_count = 0;
        U32 index_count = 0;
        B32 has_overlay_uv = false;
        for (PrimitiveNode* prim_node = primitive_list.first; prim_node; prim_node = prim_node->next)
        {
            if (prim_node->material_idx == mat_idx)
            {
                vertex_count += prim_node->vertices.size;
                index_count += prim_node->indices.size;
                has_overlay_uv |= prim_node->has_overlay_uv;
            }
        }
        render_data->render_data.index_count = index_count;
        render_data->render_data.has_overlay_uv = has_overlay_uv;

        // Second pass: copy and merge vertices/indices
        U32 vertex_offset = 0;
        U32 index_offset = 0;

        Buffer<render::TileVertex> vertices = {};
        Buffer<U32> indices = {};
        vertices = buffer_alloc<render::TileVertex>(tile_render_data_list->arena, vertex_count);
        indices = buffer_alloc<U32>(tile_render_data_list->arena, index_count);
        for (PrimitiveNode* prim_node = primitive_list.first; prim_node; prim_node = prim_node->next)
        {
            if (prim_node->material_idx == mat_idx)
            {
                MemoryCopy(vertices.data + vertex_offset, prim_node->vertices.data, prim_node->vertices.size * sizeof(*vertices.data));
                // Copy indices and adjust for vertex offset
                for (U32 i = 0; i < prim_node->indices.size; ++i)
                {
                    indices.data[index_offset + i] = prim_node->indices.data[i] + vertex_offset;
                }
                vertex_offset += prim_node->vertices.size;
                index_offset += prim_node->indices.size;
            }
        }
        render::BufferInfo vertex_info = render::BufferInfo(vertices, render::BufferType_Vertex | render::BufferType_StorageBuffer);
        render::BufferInfo index_info = render::BufferInfo(indices, render::BufferType_Index | render::BufferType_StorageBuffer);

        glm::mat4 model_matrix = glm::identity<glm::mat4>();
        render::BufferInfo model_matrix_info = render::BufferInfo(tile_arena, &model_matrix, render::BufferType_Uniform);

        render_data->render_data.vertex_buffer_handle = render::buffer_load_sync(thread_input, &vertex_info);
        render_data->render_data.index_buffer_handle = render::buffer_load_sync(thread_input, &index_info);

        // Texture Loading
        S32 tex_idx = -1;
        if (model.materials[mat_idx].pbrMetallicRoughness.has_value())
        {
            const auto& pbr = *model.materials[mat_idx].pbrMetallicRoughness;
            if (pbr.baseColorTexture.has_value())
            {
                tex_idx = pbr.baseColorTexture->index;
            }
        }

        render_data->render_data.texture_handle = render::texture_zero_handle_get();
        if (tex_idx >= 0)
        {
            CesiumGltf::Texture texture = model.textures[tex_idx];
            AssertAlways(texture.source >= 0);
            const CesiumGltf::ImageAsset& image = *model.images[texture.source].pAsset;

            // Extract sampler from glTF
            render::SamplerInfo sampler_info = {.min_filter = render::Filter_Linear,
                                                .mag_filter = render::Filter_Linear,
                                                .mip_map_mode = render::MipMapMode_Linear,
                                                .address_mode_u = render::SamplerAddressMode_Repeat,
                                                .address_mode_v = render::SamplerAddressMode_Repeat};
            if (texture.sampler >= 0 && texture.sampler < (S32)model.samplers.size())
            {
                sampler_info = sampler_info_from_cesium_sampler(model.samplers[texture.sampler]);
            }

            const std::byte* bytes = image.pixelData.data();
            U32 byte_count = (U32)image.pixelData.size();
            AssertAlways(byte_count);
            U8* tex_buffer = PushArray(tile_render_data_list->arena, U8, byte_count);
            MemoryCopy(tex_buffer, (U8*)bytes, byte_count);
            U32 width = image.width;
            U32 height = image.height;
            U32 channels = image.channels;                 // e.g., 4 for RGBA
            U32 bytes_per_channel = image.bytesPerChannel; // typically 1

            render::TextureUploadData tex_data = render::TextureUploadData::init(tex_buffer, (U32)width, (U32)height, channels, bytes_per_channel, byte_count);
            render_data->render_data.texture_handle = render::texture_load_sync(&sampler_info, &tex_data, thread_input->cmd_buffer);
        }
    }

    return tile_render_data_list;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tileset Renderer Lifecycle
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal TilesetRendererCreateContext
_tileset_renderer_create_context(Arena* arena, TilesetRenderer* renderer, async::ThreadPool* threads, F64 origin_longitude, F64 origin_latitude, F64 origin_height)
{
    // Register all tile content types
    Cesium3DTilesContent::registerAllTileContentTypes();

    renderer->tiles_to_free_mutex = os_rw_mutex_alloc();

    // Create task processor
    DTCityTaskProcessor* task_processor = PushStructNoZero(arena, DTCityTaskProcessor);
    new (task_processor) DTCityTaskProcessor(threads);
    renderer->task_processor = task_processor;
    std::shared_ptr<CesiumAsync::ITaskProcessor> task_processor_ref(renderer->task_processor, [](CesiumAsync::ITaskProcessor*) {});

    // Create asset accessor using CesiumCurl
    std::shared_ptr<CesiumAsync::IAssetAccessor> asset_accessor = std::make_shared<CesiumCurl::CurlAssetAccessor>();

    // Create async system
    new (&renderer->async_system) CesiumAsync::AsyncSystem(task_processor_ref);
    renderer->credit_system = new CesiumUtility::CreditSystem();
    std::shared_ptr<CesiumUtility::CreditSystem> credit_system_ref(renderer->credit_system, [](CesiumUtility::CreditSystem*) {});

    // Set up local coordinate system centered at the origin
    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(origin_longitude), glm::radians(origin_latitude), origin_height);

    CesiumGeospatial::LocalHorizontalCoordinateSystem local_coord_system(origin_cartographic, CesiumGeospatial::LocalDirection::East, CesiumGeospatial::LocalDirection::North,
                                                                         CesiumGeospatial::LocalDirection::Up);
    renderer->ecef_to_local = local_coord_system.getEcefToLocalTransformation();
    renderer->local_to_ecef = local_coord_system.getLocalToEcefTransformation();

    // Create prepare renderer resources (pass coordinate system for ECEF->local transforms)
    auto prepare_renderer_resources = std::make_shared<DTCityPrepareRendererResources>(renderer->ecef_to_local, renderer);

    std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
    if (logger)
    {
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
    }

    // Keep Cesium's default logger instead of overwriting it with nullptr.
    Cesium3DTilesSelection::TilesetExternals externals{asset_accessor, prepare_renderer_resources, renderer->async_system, credit_system_ref, logger, nullptr};

    // Create tileset options
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumScreenSpaceError = 16.0;
    options.maximumSimultaneousTileLoads = 20;
    options.loadingDescendantLimit = 20;
    options.loadErrorCallback = [](const Cesium3DTilesSelection::TilesetLoadFailureDetails& details)
    {
        const char* load_type = "Unknown";
        switch (details.type)
        {
            case Cesium3DTilesSelection::TilesetLoadType::CesiumIon: load_type = "CesiumIon"; break;
            case Cesium3DTilesSelection::TilesetLoadType::TilesetJson: load_type = "TilesetJson"; break;
            case Cesium3DTilesSelection::TilesetLoadType::Unknown: break;
            default: break;
        }

        exit_with_error("Cesium load error: type=%s status=%u message=%s\n", load_type, (U32)details.statusCode, details.message.c_str());
    };

    TilesetRendererCreateContext result = {renderer, externals, options};
    return result;
}

g_internal F64
_sample_height_from_result(const Cesium3DTilesSelection::SampleHeightResult& result, const char* label)
{
    F64 sampled_height = 0.0;
    if (result.positions.size() > 0)
    {
        sampled_height = result.positions[0].height;
    }

    for (const std::string& warning : result.warnings)
    {
        DEBUG_LOG("Tileset height sample [%s] warning: %s\n", label, warning.c_str());
    }
    return sampled_height;
}

// samples the custom-geometry and terrain surface heights at the city center and
// writes their delta into renderer->height_offset. both queries are driven to
// completion by the per-frame tileset_update_view; the continuation reschedules
// itself so height_offset keeps refining as more detailed tiles stream in.
// tileset_renderer_destroy sets height_sample_stop to end the loop.
g_internal void
_height_offset_sample_async(TilesetRenderer* renderer, CesiumGeospatial::Cartographic center_position)
{
    std::vector<CesiumAsync::Future<Cesium3DTilesSelection::SampleHeightResult>> height_futures;
    for (U64 i = 0; i < renderer->tilesets.size; ++i)
    {
        height_futures.emplace_back(renderer->tilesets.data[i]->sampleHeightMostDetailed({center_position}));
    }

    renderer->async_system.all(std::move(height_futures))
        .thenInMainThread(
            [renderer, center_position](std::vector<Cesium3DTilesSelection::SampleHeightResult>&& results)
            {
                F64 terrain_height = _sample_height_from_result(results[0], "terrain");
                F64 geometry_height = _sample_height_from_result(results[1], "geometry");

                B32 geometry_ok = results[0].sampleSuccess.size() > 0 && results[0].sampleSuccess[0];
                B32 terrain_ok = results[1].sampleSuccess.size() > 0 && results[1].sampleSuccess[0];
                if (geometry_ok && terrain_ok)
                {
                    renderer->height_offset = geometry_height - terrain_height;
                }

                if (!renderer->height_sample_stop)
                {
                    _height_offset_sample_async(renderer, center_position);
                }
            })
        .catchInMainThread([](std::exception&& e) { DEBUG_LOG("Height offset sampling failed: %s\n", e.what()); });
}

g_internal void
tileset_renderer_create(Arena* arena, TilesetRenderer* in_out_cesium, async::ThreadPool* threads, String8 url, F64 origin_longitude, F64 origin_latitude, F64 origin_height,
                        bool custom_geometry_enabled)
{
    // create terrain from cesium ion for non custom geometry outside the specified bounding box
    ScratchScope scratch = ScratchScope(&arena, 1);
    String8 ion_access_token = {};
    if (env_vars_value_get(scratch.arena, S("CESIUM_ION_ACCESS_TOKEN"), &ion_access_token, 1))
    {
        exit_with_error("CESIUM_ION_ACCESS_TOKEN must be set to load Cesium ion terrain");
    }

    S64 ion_terrain_asset_id = 1;
    String8 ion_terrain_asset_id_str = {};
    if (!env_vars_value_get(scratch.arena, S("CESIUM_ION_TERRAIN_ASSET_ID"), &ion_terrain_asset_id_str, 1))
    {
        ion_terrain_asset_id = s64_from_str8(ion_terrain_asset_id_str, 10);
        if (ion_terrain_asset_id <= 0)
        {
            exit_with_error("Invalid CESIUM_ION_TERRAIN_ASSET_ID value: %.*s", str8_varg(ion_terrain_asset_id_str));
        }
    }

    // setup tilesets
    TilesetRendererCreateContext create_context = _tileset_renderer_create_context(arena, in_out_cesium, threads, origin_longitude, origin_latitude, origin_height);
    create_context.options.enableLodTransitionPeriod = false;
    create_context.options.lodTransitionLength = 1.0;

    create_context.options.maximumCachedBytes = 128LL * 1024 * 1024;
    create_context.options.maximumSimultaneousTileLoads = 8;
    create_context.options.preloadSiblings = false;
    create_context.options.loadingDescendantLimit = 4;
    U32 tileset_count = 1;
    if (custom_geometry_enabled)
    {
        tileset_count = 2;
    }
    create_context.renderer->tilesets = buffer_alloc<Cesium3DTilesSelection::Tileset*>(arena, tileset_count);

    bool is_map_tile = true;
    create_context.options.rendererOptions = is_map_tile;
    create_context.renderer->tilesets.data[0] = new Cesium3DTilesSelection::Tileset(create_context.externals, ion_terrain_asset_id, _std_string_from_str8(ion_access_token), create_context.options);
    _ion_raster_overlay_example_add_if_present(create_context.renderer->tilesets.data[0]);

    // Create the tileset for custom geometry
    if (create_context.renderer->tilesets.size > 1)
    {
        is_map_tile = false;
        create_context.options.rendererOptions = is_map_tile;
        create_context.renderer->tilesets.data[1] = new Cesium3DTilesSelection::Tileset(create_context.externals, (const char*)url.str, create_context.options);
        CesiumGeospatial::Cartographic center_position(glm::radians(origin_longitude), glm::radians(origin_latitude), 0.0);
        _height_offset_sample_async(create_context.renderer, center_position);
    }
}

g_internal void
tileset_renderer_free_tile_ressource(TilesetRenderer* renderer)
{
    while (1)
    {
        TileRenderDataList* tile_data = 0;
        OS_MutexScopeW(renderer->tiles_to_free_mutex)
        {
            tile_data = renderer->tiles_to_free_stack;
            if (tile_data)
            {
                SLLStackPop(renderer->tiles_to_free_stack);
                renderer->tiles_to_free_stack_count--;
            }
        }

        if (!tile_data)
        {
            break;
        }

        for (TileRenderData* render_data = tile_data->first; render_data; render_data = render_data->next)
        {
            render::handle_destroy_deferred(render_data->render_data.vertex_buffer_handle);
            render::handle_destroy_deferred(render_data->render_data.index_buffer_handle);
            render::Handle null_texture_handle = render::texture_zero_handle_get();
            if (render_data->render_data.texture_handle.u64 != null_texture_handle.u64 || render_data->render_data.texture_handle.gen_id != null_texture_handle.gen_id ||
                render_data->render_data.texture_handle.type != null_texture_handle.type)
            {
                render::handle_destroy_deferred(render_data->render_data.texture_handle);
            }
            render::handle_destroy_deferred(render_data->render_data.overlay_texture_handle);
        }
        arena_release(tile_data->arena);
    }
}

g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer)
{
    if (renderer)
    {
        // stop the recurring height sampler before tearing down the tilesets
        renderer->height_sample_stop = true;

        for (U32 i = 0; i < renderer->tilesets.size; ++i)
        {
            if (renderer->tilesets.data[i]->waitForAllLoadsToComplete(max_f32) == false)
            {
                DEBUG_LOG("Failed to wait for all tile loads to complete");
            }

            delete renderer->tilesets.data[i];
            renderer->tilesets.data[i] = nullptr;
        }

        renderer->async_system.~AsyncSystem();

        delete renderer->credit_system;
        renderer->credit_system = nullptr;

        ((DTCityTaskProcessor*)renderer->task_processor)->~DTCityTaskProcessor();
        renderer->task_processor = nullptr;
    }

    tileset_renderer_free_tile_ressource(renderer);
    OS_RWMutexRelease(renderer->tiles_to_free_mutex);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Update and Rendering
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Advances completed async work without touching the view. Inactive renderers
// must still drain freed tile resources, but should not kick more tile loads.
g_internal void
tileset_pump_async(TilesetRenderer* renderer)
{
    if (!renderer)
        return;

    renderer->async_system.dispatchMainThreadTasks();
    tileset_renderer_free_tile_ressource(renderer);
}

g_internal void
tileset_update_view(TilesetRenderer* renderer, ui::Camera* camera, Vec2U32 viewport_size, F64 delta_time)
{
    prof_scope_marker;
    if (!renderer)
        return;

    // Get camera position in ECEF
    glm::dmat4 local_to_ecef = renderer->local_to_ecef;
    glm::dvec3 camera_pos_local = glm::dvec3(camera->position);
    glm::dvec3 camera_pos_ecef = glm::dvec3(local_to_ecef * glm::dvec4(camera_pos_local, 1.0));

    // Get camera direction in ECEF
    glm::dvec3 camera_dir_local = glm::dvec3(camera->view_dir);
    glm::dvec3 camera_dir_ecef = glm::normalize(glm::dvec3(local_to_ecef * glm::dvec4(camera_dir_local, 0.0)));

    // Get camera up in ECEF (local Z-up -> ECEF)
    glm::dvec3 camera_up_local = glm::dvec3(0.0, 0.0, 1.0);
    glm::dvec3 camera_up_ecef = glm::normalize(glm::dvec3(local_to_ecef * glm::dvec4(camera_up_local, 0.0)));

    // Compute unflipped projection matrix for Cesium (without Vulkan Y-flip)
    F64 aspect_ratio = (F64)viewport_size.x / (F64)viewport_size.y;
    F64 fov_rad = glm::radians((F64)camera->fov);

    // Use the position/direction/up ViewState constructor
    Cesium3DTilesSelection::ViewState view_state = Cesium3DTilesSelection::ViewState(camera_pos_ecef, camera_dir_ecef, camera_up_ecef, glm::dvec2(viewport_size.x, viewport_size.y),
                                                                                     fov_rad * aspect_ratio, // horizontal FOV
                                                                                     fov_rad);               // vertical FOV

    renderer->async_system.dispatchMainThreadTasks();

    // Clear and rebuild the loaded tiles list
    renderer->tile_to_show = {};
    renderer->tiles_to_show_count = 0;
    // Update the tileset view
    std::vector<Cesium3DTilesSelection::ViewState> views = {view_state};
    tileset_renderer_free_tile_ressource(renderer);
    for (U32 i = 0; i < renderer->tilesets.size; ++i)
    {
        const Cesium3DTilesSelection::ViewUpdateResult& result = renderer->tilesets.data[i]->updateViewGroup(renderer->tilesets.data[i]->getDefaultViewGroup(), views, (F32)delta_time);
        renderer->tilesets.data[i]->loadTiles();

        for (const Cesium3DTilesSelection::Tile* tile : result.tilesToRenderThisFrame)
        {
            if (tile->getState() != Cesium3DTilesSelection::TileLoadState::Done)
            {
                continue;
            }

            const Cesium3DTilesSelection::TileContent& content = tile->getContent();
            const Cesium3DTilesSelection::TileRenderContent* render_content = content.getRenderContent();

            if (!render_content)
            {
                continue;
            }

            void* renderer_resources = render_content->getRenderResources();
            if (!renderer_resources)
                continue;

            TileRenderDataList* render_data = static_cast<TileRenderDataList*>(renderer_resources);
            if (!render_data)
            {
                continue;
            }

            if (render_data->tile_is_loaded)
            {
                prof_scope_marker_named("tileset_update_view:schedule_loaded_tiles");
                _tile_render_data_overlay_apply(render_data);
                for (TileRenderData* render_data_node = render_data->first; render_data_node; render_data_node = render_data_node->next)
                {
                    SLLQueuePush_N(renderer->tile_to_show.first, renderer->tile_to_show.last, render_data_node, render_next);
                    renderer->tiles_to_show_count++;
                }
            }
        }
    }
}

g_internal B32
root_tileset_url_is_readable(String8 url)
{
    if (url.size == 0)
    {
        return false;
    }

    CesiumUtility::Uri uri((const char*)url.str);
    if (uri.getScheme() != "file:")
    {
        return false;
    }

    std::string native_path = CesiumUtility::Uri::uriPathToNativePath(std::string(uri.getPath()));
    return os_file_path_exists(Str8((U8*)native_path.data(), native_path.size()));
}
} // namespace cesium
