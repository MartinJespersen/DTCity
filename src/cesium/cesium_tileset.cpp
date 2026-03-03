namespace cesium
{

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task Processor Implementation
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class DTCityTaskProcessor : public CesiumAsync::ITaskProcessor
{
  public:
    DTCityTaskProcessor(async::Queue<async::QueueItem>* queue) : queue(queue)
    {
    }

    void
    startTask(std::function<void()> task) override
    {
        // Create a copy of the task on the heap
        auto* task_copy = new std::function<void()>(std::move(task));

        async::QueueItem item = {};
        item.data = task_copy;
        item.worker_func = [](async::ThreadInfo, void* data)
        {
            // ~mgj: Enqueue the command buffer
            auto* func = static_cast<std::function<void()>*>(data);
            (*func)();
            delete func;
        };

        if (!async::QueueTryPush(queue, &item))
        {
            exit_with_error("Failed to enqueue task");
            (*task_copy)();
            delete task_copy;
        }
    }

  private:
    async::Queue<async::QueueItem>* queue;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Prepare Renderer Resources Implementation
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class DTCityPrepareRendererResources : public Cesium3DTilesSelection::IPrepareRendererResources
{
  public:
    DTCityPrepareRendererResources(glm::dmat4 ecef_to_local_transform,
                                   TilesetRenderer* tile_set_renderer)
        : ecef_to_local_transform(ecef_to_local_transform), tile_set_renderer(tile_set_renderer)
    {
    }

    CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources>
    prepareInLoadThread(const CesiumAsync::AsyncSystem& asyncSystem,
                        Cesium3DTilesSelection::TileLoadResult&& tileLoadResult,
                        const glm::dmat4& transform, const std::any& rendererOptions) override
    {
        (void)rendererOptions;

        CesiumGltf::Model* pModel = std::get_if<CesiumGltf::Model>(&tileLoadResult.contentKind);

        if (!pModel)
        {
            return asyncSystem.createResolvedFuture(
                Cesium3DTilesSelection::TileLoadResultAndRenderResources{std::move(tileLoadResult),
                                                                         nullptr});
        }

        render::ThreadInput* thread_input = render::thread_input_create();
        TileInfo tile_info = TileInfo(*pModel, ecef_to_local_transform, transform);
        render::ThreadSyncCallback callback =
            render::ThreadSyncCallback(&tile_info, render_list_record);
        void* render_data_list = render::thread_cmd_buffer_record(thread_input, callback);

        return asyncSystem.createResolvedFuture(
            Cesium3DTilesSelection::TileLoadResultAndRenderResources{std::move(tileLoadResult),
                                                                     render_data_list});
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
    free(Cesium3DTilesSelection::Tile& tile, void* pLoadThreadResult,
         void* pMainThreadResult) noexcept override
    {
        prof_scope_marker;
        (void)tile;
        (void)pLoadThreadResult;

        TileRenderDataList* render_data_list = static_cast<TileRenderDataList*>(
            pMainThreadResult ? pMainThreadResult : pLoadThreadResult);

        if (!render_data_list)
            return;

        OS_MutexScopeW(tile_set_renderer->tiles_to_free_mutex)
        {
            SLLStackPush(tile_set_renderer->tiles_to_free_stack, render_data_list);
            tile_set_renderer->tiles_to_free_stack_count++;
        }
    }

    void*
    prepareRasterInLoadThread(CesiumGltf::ImageAsset& image,
                              const std::any& rendererOptions) override
    {
        (void)image;
        (void)rendererOptions;
        return nullptr;
    }

    void*
    prepareRasterInMainThread(CesiumRasterOverlays::RasterOverlayTile& rasterTile,
                              void* pLoadThreadResult) override
    {
        (void)rasterTile;
        (void)pLoadThreadResult;
        return nullptr;
    }

    void
    freeRaster(const CesiumRasterOverlays::RasterOverlayTile& rasterTile, void* pLoadThreadResult,
               void* pMainThreadResult) noexcept override
    {
        (void)rasterTile;
        (void)pLoadThreadResult;
        (void)pMainThreadResult;
    }

    void
    attachRasterInMainThread(const Cesium3DTilesSelection::Tile& tile,
                             int32_t overlayTextureCoordinateID,
                             const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
                             void* pMainThreadRendererResources, const glm::dvec2& translation,
                             const glm::dvec2& scale) override
    {
        (void)tile;
        (void)overlayTextureCoordinateID;
        (void)rasterTile;
        (void)pMainThreadRendererResources;
        (void)translation;
        (void)scale;
    }

    void
    detachRasterInMainThread(const Cesium3DTilesSelection::Tile& tile,
                             int32_t overlayTextureCoordinateID,
                             const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
                             void* pMainThreadRendererResources) noexcept override
    {
        (void)tile;
        (void)overlayTextureCoordinateID;
        (void)rasterTile;
        (void)pMainThreadRendererResources;
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
            case CesiumGltf::Sampler::MagFilter::NEAREST:
                mag_filter = render::Filter_Nearest;
                break;
            case CesiumGltf::Sampler::MagFilter::LINEAR: mag_filter = render::Filter_Linear; break;
            default: InvalidPath; break;
        }
    }

    switch (sampler.wrapS)
    {
        case CesiumGltf::Sampler::WrapS::CLAMP_TO_EDGE:
            address_mode_u = render::SamplerAddressMode_ClampToEdge;
            break;
        case CesiumGltf::Sampler::WrapS::MIRRORED_REPEAT:
            address_mode_u = render::SamplerAddressMode_MirroredRepeat;
            break;
        case CesiumGltf::Sampler::WrapS::REPEAT:
            address_mode_u = render::SamplerAddressMode_Repeat;
            break;
        default: InvalidPath; break;
    }

    switch (sampler.wrapT)
    {
        case CesiumGltf::Sampler::WrapT::CLAMP_TO_EDGE:
            address_mode_v = render::SamplerAddressMode_ClampToEdge;
            break;
        case CesiumGltf::Sampler::WrapT::MIRRORED_REPEAT:
            address_mode_v = render::SamplerAddressMode_MirroredRepeat;
            break;
        case CesiumGltf::Sampler::WrapT::REPEAT:
            address_mode_v = render::SamplerAddressMode_Repeat;
            break;
        default: InvalidPath; break;
    }

    render::SamplerInfo sampler_info = {.min_filter = min_filter,
                                        .mag_filter = mag_filter,
                                        .mip_map_mode = mipmap_mode,
                                        .address_mode_u = address_mode_u,
                                        .address_mode_v = address_mode_v};
    return sampler_info;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper: Convert Cesium glTF to render data
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal void*
render_list_record(render::ThreadInput* thread_input, render::FuncData user_data)
{
    TileInfo* tile_info = (TileInfo*)user_data;
    TileRenderDataList* render_data_list = tile_render_data_from_gltf(
        tile_info->model, tile_info->ecef_to_local, tile_info->tile_transform, thread_input);
    {
        auto stub_func = [](void* data, render::ThreadInput* thread_input)
        {
            (void)data;
            (void)thread_input;
        };

        for (TileRenderData* data = render_data_list->first; data; data = data->next)
        {
            render::handle_list_push(thread_input->arena, &thread_input->handles,
                                     data->render_data.vertex_buffer_handle);
            render::handle_list_push(thread_input->arena, &thread_input->handles,
                                     data->render_data.index_buffer_handle);
            render::handle_list_push(thread_input->arena, &thread_input->handles,
                                     data->render_data.texture_handle);

            render::handle_list_push(thread_input->arena, &thread_input->handles,
                                     data->render_data.storage_buffer_handle);

            thread_input->done_loading_func = render::handle_done_loading;
            thread_input->loading_func = stub_func;
        }
    }
    return render_data_list;
}

g_internal TileRenderDataList*
tile_render_data_from_gltf(const CesiumGltf::Model& model, const glm::dmat4& ecef_to_local,
                           const glm::dmat4& tile_transform, render::ThreadInput* thread_input)
{
    prof_scope_marker;

    ScratchScope scratch = ScratchScope(0, 0);
    Arena* tile_arena = arena_alloc();
    TileRenderDataList* tile_render_data_list = PushStruct(tile_arena, TileRenderDataList);
    tile_render_data_list->arena = tile_arena;

    struct PrimitiveNode
    {
        PrimitiveNode* next;
        Buffer<render::Vertex3D> vertices;
        Buffer<U32> indices;
        U32 material_idx;
    };

    struct PrimitiveList
    {
        PrimitiveNode* first;
        PrimitiveNode* last;
    };

    Assert(model.scenes.size() == 1);
    for (U32 i = 0; i < model.nodes.size(); i++)
    {
        Assert(model.nodes[i].children.size() == 0);
    }
    PrimitiveList primitive_list = {};
    for (const CesiumGltf::Mesh& mesh : model.meshes)
    {
        for (const CesiumGltf::MeshPrimitive& primitive : mesh.primitives)
        {
            auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end())
                continue;

            const CesiumGltf::Accessor* pos_accessor =
                CesiumGltf::Model::getSafe(&model.accessors, pos_it->second);
            if (!pos_accessor)
                continue;

            // Get UV accessor if available
            const CesiumGltf::Accessor* uv_accessor = nullptr;
            auto uv_it = primitive.attributes.find("TEXCOORD_0");
            if (uv_it != primitive.attributes.end())
            {
                uv_accessor = CesiumGltf::Model::getSafe(&model.accessors, uv_it->second);
            }

            // Get buffer views and buffers for position
            const CesiumGltf::BufferView* pos_buffer_view =
                CesiumGltf::Model::getSafe(&model.bufferViews, pos_accessor->bufferView);
            if (!pos_buffer_view)
                continue;

            const CesiumGltf::Buffer* pos_buffer =
                CesiumGltf::Model::getSafe(&model.buffers, pos_buffer_view->buffer);
            if (!pos_buffer)
                continue;

            // Read positions
            const U8* pos_data = (U8*)pos_buffer->cesium.data.data() + pos_buffer_view->byteOffset +
                                 pos_accessor->byteOffset;

            // Stride cannot change dynamically with current implementation
            S64 pos_stride = pos_buffer_view->byteStride.value_or(0);
            Assert(pos_stride == 0 || pos_stride == (sizeof(F32) * 3));
            pos_stride = sizeof(F32) * 3;

            // Get UV data if available
            const U8* uv_data = nullptr;
            U32 uv_stride = 0;
            if (uv_accessor)
            {
                const CesiumGltf::BufferView* uv_buffer_view =
                    CesiumGltf::Model::getSafe(&model.bufferViews, uv_accessor->bufferView);
                if (uv_buffer_view)
                {
                    const CesiumGltf::Buffer* uv_buffer =
                        CesiumGltf::Model::getSafe(&model.buffers, uv_buffer_view->buffer);
                    if (uv_buffer)
                    {
                        uv_data = (U8*)uv_buffer->cesium.data.data() + uv_buffer_view->byteOffset +
                                  uv_accessor->byteOffset;
                        U32 uv_byte_stride = uv_buffer_view->byteStride.value_or(0);
                        uv_stride = uv_byte_stride > 0 ? uv_byte_stride : sizeof(F32) * 2;
                    }
                }
            }

            const CesiumGltf::Accessor* index_accessor =
                CesiumGltf::Model::getSafe(&model.accessors, primitive.indices);

            // Allocate buffers
            Buffer<render::Vertex3D> vertices =
                BufferAlloc<render::Vertex3D>(scratch.arena, pos_accessor->count);
            Buffer<U32> indices = BufferAlloc<U32>(scratch.arena, index_accessor->count);

            // Copy vertices
            for (U32 i = 0; i < (U32)pos_accessor->count; ++i)
            {
                render::Vertex3D* vertex = &vertices.data[i];

                // glTF uses right-handed Y-up, convert to right-handed Z-up: (x, y, z) -> (x, z,
                // -y) Then transform applies: tile_transform (to ECEF) -> ecef_to_local (to local
                // ENU)
                const F32* pos = (const F32*)(pos_data + i * pos_stride);
                glm::dvec4 pos_zup =
                    ecef_to_local * tile_transform * glm::dvec4(pos[0], -pos[2], pos[1], 1.0);

                vertex->pos.x = (F32)pos_zup.x;
                vertex->pos.y = (F32)pos_zup.y;
                vertex->pos.z = (F32)pos_zup.z;

                AssertAlways(uv_data);
                const F32* uv = (const F32*)(uv_data + (U64)i * (U64)uv_stride);
                vertex->uv.x = uv[0];
                vertex->uv.y = uv[1];
            }

            // Read indices
            if (primitive.indices >= 0)
            {
                if (index_accessor)
                {
                    const CesiumGltf::BufferView* index_buffer_view =
                        CesiumGltf::Model::getSafe(&model.bufferViews, index_accessor->bufferView);
                    if (index_buffer_view)
                    {
                        const CesiumGltf::Buffer* index_buffer =
                            CesiumGltf::Model::getSafe(&model.buffers, index_buffer_view->buffer);
                        if (index_buffer)
                        {
                            U8* index_data = (U8*)index_buffer->cesium.data.data() +
                                             index_buffer_view->byteOffset +
                                             index_accessor->byteOffset;

                            U32 stride = index_accessor->componentType ==
                                                 CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE
                                             ? sizeof(U8)
                                         : index_accessor->componentType ==
                                                 CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT
                                             ? sizeof(U16)
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

            SLLQueuePush(primitive_list.first, primitive_list.last, primitive_node);
        }
    }

    Assert(model.textures.size() > 0);
    Assert(model.images.size() > 0);
    for (U32 mat_idx = 0; mat_idx < model.materials.size(); ++mat_idx)
    {
        TileRenderData* render_data = PushStruct(tile_render_data_list->arena, TileRenderData);
        SLLQueuePush(tile_render_data_list->first, tile_render_data_list->last, render_data);

        // Create and async load index and vertex buffer
        // First pass: count vertices/indices only for primitives with this material
        U32 vertex_count = 0;
        U32 index_count = 0;
        for (PrimitiveNode* prim_node = primitive_list.first; prim_node;
             prim_node = prim_node->next)
        {
            if (prim_node->material_idx == mat_idx)
            {
                vertex_count += prim_node->vertices.size;
                index_count += prim_node->indices.size;
            }
        }
        render_data->render_data.index_count = index_count;

        // Second pass: copy and merge vertices/indices
        U32 vertex_offset = 0;
        U32 index_offset = 0;

        Buffer<render::Vertex3D> vertices = {};
        Buffer<U32> indices = {};
        vertices = BufferAlloc<render::Vertex3D>(tile_render_data_list->arena, vertex_count);
        indices = BufferAlloc<U32>(tile_render_data_list->arena, index_count);
        for (PrimitiveNode* prim_node = primitive_list.first; prim_node;
             prim_node = prim_node->next)
        {
            if (prim_node->material_idx == mat_idx)
            {
                MemoryCopy(vertices.data + vertex_offset, prim_node->vertices.data,
                           prim_node->vertices.size * sizeof(*vertices.data));
                // Copy indices and adjust for vertex offset
                for (U32 i = 0; i < prim_node->indices.size; ++i)
                {
                    indices.data[index_offset + i] = prim_node->indices.data[i] + vertex_offset;
                }
                vertex_offset += prim_node->vertices.size;
                index_offset += prim_node->indices.size;
            }
        }
        render::BufferInfo vertex_info = render::BufferInfo(
            vertices, render::BufferType_Vertex | render::BufferType_StorageBuffer);
        render::BufferInfo index_info = render::BufferInfo(
            indices, render::BufferType_Index | render::BufferType_StorageBuffer);

        glm::mat4 model_matrix = glm::identity<glm::mat4>();
        render::BufferInfo model_matrix_info =
            render::BufferInfo(tile_arena, &model_matrix, render::BufferType_Uniform);

        render_data->render_data.vertex_buffer_handle =
            render::buffer_load_sync((VkCommandBuffer)thread_input->cmd_buffer, &vertex_info);
        render_data->render_data.index_buffer_handle =
            render::buffer_load_sync((VkCommandBuffer)thread_input->cmd_buffer, &index_info);
        render_data->render_data.storage_buffer_handle = render::storage_buffer_load_sync(
            thread_input->arena, render_data->render_data.vertex_buffer_handle,
            render_data->render_data.index_buffer_handle);

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
        AssertAlways(tex_idx >= 0);
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

        render::TextureUploadData tex_data = render::TextureUploadData::init(
            tex_buffer, (U32)width, (U32)height, channels, bytes_per_channel, byte_count);
        render_data->render_data.texture_handle = render::texture_load_sync(
            &sampler_info, &tex_data, (VkCommandBuffer)thread_input->cmd_buffer);
    }

    return tile_render_data_list;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tileset Renderer Lifecycle
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal TilesetRenderer*
tileset_renderer_create(Arena* arena, async::Threads* threads, const char* tileset_url,
                        F64 origin_longitude, F64 origin_latitude, F64 origin_height)
{
    // Register all tile content types
    Cesium3DTilesContent::registerAllTileContentTypes();

    TilesetRenderer* renderer = PushArrayNoZero(arena, TilesetRenderer, 1);
    renderer->tiles_to_free_mutex = OS_RWMutexAlloc();

    // Create task processor
    renderer->task_processor = std::make_shared<DTCityTaskProcessor>(threads->msg_queue);

    // Create asset accessor using CesiumCurl
    std::shared_ptr<CesiumAsync::IAssetAccessor> asset_accessor =
        std::make_shared<CesiumCurl::CurlAssetAccessor>();

    // Create async system
    new (&renderer->async_system) CesiumAsync::AsyncSystem(renderer->task_processor);

    // Set up local coordinate system centered at the origin
    CesiumGeospatial::Cartographic origin_cartographic(
        glm::radians(origin_longitude), glm::radians(origin_latitude), origin_height);

    renderer->local_coord_system = new CesiumGeospatial::LocalHorizontalCoordinateSystem(
        origin_cartographic, CesiumGeospatial::LocalDirection::East,
        CesiumGeospatial::LocalDirection::North, CesiumGeospatial::LocalDirection::Up);

    // Create prepare renderer resources (pass coordinate system for ECEF->local transforms)
    auto prepare_renderer_resources = std::make_shared<DTCityPrepareRendererResources>(
        renderer->local_coord_system->getEcefToLocalTransformation(), renderer);

    // Create tileset externals
    Cesium3DTilesSelection::TilesetExternals externals{
        asset_accessor, prepare_renderer_resources, renderer->async_system, nullptr, nullptr,
        nullptr};

    // Create tileset options
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumScreenSpaceError = 16.0;
    options.maximumSimultaneousTileLoads = 20;
    options.loadingDescendantLimit = 20;

    // Create the tileset
    renderer->tileset =
        std::make_shared<Cesium3DTilesSelection::Tileset>(externals, tileset_url, options);

    return renderer;
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

        for (TileRenderData* render_data = tile_data->first; render_data;
             render_data = render_data->next)
        {
            render::handle_destroy_deferred(render_data->render_data.vertex_buffer_handle);
            render::handle_destroy_deferred(render_data->render_data.index_buffer_handle);
            render::handle_destroy_deferred(render_data->render_data.texture_handle);
            render::handle_destroy_deferred(render_data->render_data.storage_buffer_handle);
        }
        arena_release(tile_data->arena);
    }
}

g_internal void
tileset_renderer_destroy(TilesetRenderer* renderer)
{
    if (renderer)
    {
        if (renderer->tileset->waitForAllLoadsToComplete(max_f32) == false)
        {
            DEBUG_LOG("Failed to wait for all tile loads to complete");
        }

        renderer->tileset.reset();
        renderer->task_processor.reset();
    }

    tileset_renderer_free_tile_ressource(renderer);
    OS_RWMutexRelease(renderer->tiles_to_free_mutex);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Update and Rendering
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

g_internal void
tileset_update_view(Arena* arena, TilesetRenderer* renderer, ui::Camera* camera,
                    Vec2U32 viewport_size, F64 delta_time)
{
    prof_scope_marker;
    if (!renderer || !renderer->tileset)
        return;

    // Get camera position in ECEF
    glm::dmat4 local_to_ecef = renderer->local_coord_system->getLocalToEcefTransformation();
    glm::dvec3 camera_pos_local = glm::dvec3(camera->position);
    glm::dvec3 camera_pos_ecef = glm::dvec3(local_to_ecef * glm::dvec4(camera_pos_local, 1.0));

    // Get camera direction in ECEF
    glm::dvec3 camera_dir_local = glm::dvec3(camera->view_dir);
    glm::dvec3 camera_dir_ecef =
        glm::normalize(glm::dvec3(local_to_ecef * glm::dvec4(camera_dir_local, 0.0)));

    // Get camera up in ECEF (local Z-up -> ECEF)
    glm::dvec3 camera_up_local = glm::dvec3(0.0, 0.0, 1.0);
    glm::dvec3 camera_up_ecef =
        glm::normalize(glm::dvec3(local_to_ecef * glm::dvec4(camera_up_local, 0.0)));

    // Compute unflipped projection matrix for Cesium (without Vulkan Y-flip)
    F64 aspect_ratio = (F64)viewport_size.x / (F64)viewport_size.y;
    F64 fov_rad = glm::radians((F64)camera->fov);

    // Use the position/direction/up ViewState constructor
    Cesium3DTilesSelection::ViewState view_state =
        Cesium3DTilesSelection::ViewState(camera_pos_ecef, camera_dir_ecef, camera_up_ecef,
                                          glm::dvec2(viewport_size.x, viewport_size.y),
                                          fov_rad * aspect_ratio, // horizontal FOV
                                          fov_rad);               // vertical FOV

    // Update the tileset view
    std::vector<Cesium3DTilesSelection::ViewState> views = {view_state};
    const Cesium3DTilesSelection::ViewUpdateResult& result = renderer->tileset->updateViewGroup(
        renderer->tileset->getDefaultViewGroup(), views, (F32)delta_time);

    // IMPORTANT: Dispatch main thread tasks to process completed async work
    // This calls prepareInMainThread for tiles that finished loading
    renderer->async_system.dispatchMainThreadTasks();

    renderer->tileset->loadTiles();

    // Clear and rebuild the loaded tiles list
    renderer->tile_to_show = {};
    renderer->tiles_to_show_count = 0;

    tileset_renderer_free_tile_ressource(renderer);
    for (const Cesium3DTilesSelection::Tile* tile : result.tilesToRenderThisFrame)
    {
        if (tile->getState() != Cesium3DTilesSelection::TileLoadState::Done)
        {
            continue;
        }

        const Cesium3DTilesSelection::TileContent& content = tile->getContent();
        const Cesium3DTilesSelection::TileRenderContent* render_content =
            content.getRenderContent();

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
            for (TileRenderData* render_data_node = render_data->first; render_data_node;
                 render_data_node = render_data_node->next)
            {
                SLLQueuePush_N(renderer->tile_to_show.first, renderer->tile_to_show.last,
                               render_data_node, render_next);
                renderer->tiles_to_show_count++;
            }
        }
    }
}

g_internal void
tileset_render(TilesetRenderer* renderer, render::Handle road_segment_handle,
               render::Handle road_segment_node_buffer_handle,
               render::Handle road_segment_buffer_handle)
{
    if (!renderer)
        return;

    for (TileRenderData* tile = renderer->tile_to_show.first; tile; tile = tile->render_next)
    {
        if (tile->compute_scheduled == false)
        {
            tile->compute_scheduled = render::road_intersection_compute_add(
                tile->render_data.storage_buffer_handle, tile->render_data.index_buffer_handle,
                road_segment_buffer_handle, road_segment_node_buffer_handle, road_segment_handle);
        }

        if (tile->compute_scheduled)
        {
            render::model_3d_draw(tile->render_data);
        }
    }
}

} // namespace cesium
