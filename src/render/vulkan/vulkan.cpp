namespace vulkan
{

// ~mgj: global context
static Context* g_vk_ctx = 0;

// ~mgj: vulkan context

static void
ctx_set(Context* vk_ctx)
{
    Assert(!g_vk_ctx);
    g_vk_ctx = vk_ctx;
}

static void
ctx_release()
{
    g_vk_ctx = 0;
}

g_internal Context*
ctx_get()
{
    Assert(g_vk_ctx);
    return g_vk_ctx;
}

g_internal DescriptorSetInfo
descriptor_set_road_segment(VkDevice device, VkDescriptorPool desc_pool, void* data)
{
    RoadSegmentDescriptor* buffer_desc = (RoadSegmentDescriptor*)data;

    VkDescriptorSetLayout desc_set_layout = descriptor_set_layout_create(
        device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}});

    VkDescriptorSet desc_set = descriptor_set_alloc(device, desc_pool, {desc_set_layout});

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer_desc->road_segment_buffer;
    buffer_info.offset = 0;
    buffer_info.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo buffer_node_info{};
    buffer_node_info.buffer = buffer_desc->road_segment_node_buffer;
    buffer_node_info.offset = 0;
    buffer_node_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    VkWriteDescriptorSet write_node{};
    write_node.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_node.dstSet = desc_set;
    write_node.dstBinding = 1;
    write_node.dstArrayElement = 0;
    write_node.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_node.descriptorCount = 1;
    write_node.pBufferInfo = &buffer_node_info;

    descriptor_set_update(device, {write, write_node});

    return DescriptorSetInfo(desc_set, desc_set_layout);
}

static void
road_intersection_bucket_add(VkDescriptorSet storage_buffer_set, VkDescriptorSet road_segment, BufferHandle* vertex_buffer, BufferHandle* index_buffer, U32 overlay_option)
{
    Context* vk_ctx = ctx_get();
    RoadIntersectionNode* node = PushStruct(vk_ctx->render_frame_arena, RoadIntersectionNode);
    node->vertex_and_index_set = road_segment;
    node->storage_buffer_set = storage_buffer_set;
    node->vertex_buffer = *vertex_buffer;
    node->index_buffer = *index_buffer;
    node->overlay_option_idx = overlay_option;
    SLLQueuePush(vk_ctx->render_frame->road_intersection_list.first, vk_ctx->render_frame->road_intersection_list.last, node);
}

g_internal void
car_instance_compute()
{
    Context* vk_ctx = ctx_get();
    RenderFrame* render_frame = vk_ctx->render_frame;
    CarInstanceCompute* car_instance_draw = &render_frame->car_instance_compute_list;
    CarInstanceComputeNodeList* list = &car_instance_draw->list;

    if (!list->first)
    {
        return;
    }

    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "car_height_calculate_compute");

    Pipeline* pipeline = &vk_ctx->car_height_calculate_pipeline;
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

    render::AssetItem<BufferHandle>* instance_buffer_handle = asset_manager_buffer_item_get(vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame]);
    if (!instance_buffer_handle)
        return;
    BufferAllocation instance_buffer_alloc = instance_buffer_handle->item.buffer_alloc;
    VkBuffer instance_buffer = instance_buffer_alloc.buffer;

    for (CarInstanceComputeNode* node = list->first; node; node = node->next)
    {
        vkCmdPushConstants(cmd_buffer, pipeline->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CarHeightCalculatePushConstants), &node->compute_push_constants);

        VkDescriptorBufferInfo buffer_infos[] = {
            {.buffer = instance_buffer, .offset = node->instance_buffer_offset, .range = node->instance_buffer_info.buffer.size},
            {.buffer = node->tile_vertex_handle->buffer_alloc.buffer, .offset = 0, .range = VK_WHOLE_SIZE},
            {.buffer = node->tile_index_handle->buffer_alloc.buffer, .offset = 0, .range = VK_WHOLE_SIZE},
        };

        VkWriteDescriptorSet writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer_infos[0]},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer_infos[1]},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer_infos[2]},
        };
        cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline_layout, 0, ArrayCount(writes), writes);

        U32 triangle_count = node->tile_index_handle->elem_count / 3;
        U32 workgroup_count = (triangle_count + 255) / 256;
        vkCmdDispatch(cmd_buffer, workgroup_count, 1, 1);
    }
}

g_internal void
road_intersection_compute()
{
    Context* vk_ctx = ctx_get();
    RenderFrame* render_frame = vk_ctx->render_frame;
    RoadIntersectionList* list = &render_frame->road_intersection_list;

    if (!list->first)
    {
        return;
    }

    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer,
                "road_intersection_compute"); // NOLINT

    Pipeline* pipeline = &vk_ctx->road_intersection_pipeline;

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

    for (RoadIntersectionNode* node = list->first; node; node = node->next)
    {
        U32 triangle_count = node->index_buffer.elem_count / 3;
        RoadIntersectionPushConstants push_constants = {};
        push_constants.road_segment_buffer_elem_count = node->vertex_buffer.elem_count;
        push_constants.overlay_option_idx = node->overlay_option_idx;

        vkCmdPushConstants(cmd_buffer, pipeline->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RoadIntersectionPushConstants), &push_constants);

        VkDescriptorSet desc_sets[] = {node->vertex_and_index_set, node->storage_buffer_set};
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline_layout, 0, ArrayCount(desc_sets), desc_sets, 0, NULL);

        U32 workgroup_count = (triangle_count + 255) / 256; // 256 is the workgroup size specified in the shader
        vkCmdDispatch(cmd_buffer, workgroup_count, 1, 1);

        VkBufferMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                                          .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                                          .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .buffer = node->vertex_buffer.buffer_alloc.buffer,
                                          .offset = 0,
                                          .size = node->vertex_buffer.buffer_alloc.size};
        VkDependencyInfo dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};

        vkCmdPipelineBarrier2(cmd_buffer, &dep_info);
    }

} // namespace vulkan

static void
car_instance_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "car_instance_rendering");

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    Pipeline* pipeline = &vk_ctx->car_instance_pipeline;

    // prepare pipeline
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)swapchain_extent.width;
    viewport.height = (F32)swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    render::AssetItem<BufferHandle>* instance_buffer_handle = asset_manager_buffer_item_get(vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame]);
    if (!instance_buffer_handle)
    {
        return;
    }

    BufferAllocation instance_buffer_alloc = instance_buffer_handle->item.buffer_alloc;
    VkBuffer instance_buffer = instance_buffer_alloc.buffer;
    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame], vk_ctx->bindless_descriptor_set};

    for (CarInstanceRenderNode* node = vk_ctx->render_frame->car_instance_render_list.list.first; node; node = node->next)
    {
        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(vk_ctx->asset_manager->allocator, node->instance_buffer_info.buffer.data, instance_buffer_alloc.allocation, node->instance_buffer_offset,
                                                  node->instance_buffer_info.buffer.size));

        // set up rest of pipeline
        VkBuffer vertex_buffers[] = {
            node->vertex_handle->buffer_alloc.buffer,
            instance_buffer,
        };

        vkCmdPushConstants(cmd_buffer, pipeline->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CarInstancePushConstants), &node->draw_push_constants);
        VkDeviceSize vertex_offsets[] = {0, node->instance_buffer_offset};
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, vertex_offsets);
        U32 instance_count = U32(node->instance_buffer_info.buffer.size / node->instance_buffer_info.type_size);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_handle->buffer_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_buffer, node->index_handle->elem_count, instance_count, 0, 0, 0);
    }
}

// ~mgj: Camera functions
static void
camera_uniform_buffer_create(Context* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk_ctx->camera_buffer_alloc_mapped[i] = buffer_mapped_create(camera_buffer_size, buffer_usage, "camera uniform buffer");
    }
}

static void
camera_uniform_buffer_update(Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res, U32 current_frame)
{
    BufferAllocationMapped* buffer = &vk_ctx->camera_buffer_alloc_mapped[current_frame];
    CameraUniformBuffer* ubo = (CameraUniformBuffer*)buffer->mapped_ptr;
    glm::mat4 transform = camera->projection_matrix * camera->view_matrix;
    frustum_planes_calculate(&ubo->frustum, transform);
    ubo->viewport_dim.x = screen_res.x;
    ubo->viewport_dim.y = screen_res.y;
    ubo->view = camera->view_matrix;
    ubo->proj = camera->projection_matrix;

    buffer_mapped_update(vk_ctx->command_buffers.data[current_frame], *buffer);
}

static void
camera_descriptor_set_layout_create(Context* vk_ctx)
{
    VkDescriptorSetLayoutBinding camera_desc_layout{};
    camera_desc_layout.binding = 0;
    camera_desc_layout.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camera_desc_layout.descriptorCount = 1;
    camera_desc_layout.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[] = {camera_desc_layout};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = ArrayCount(descriptor_layout_bindings);
    layoutInfo.pBindings = descriptor_layout_bindings;

    if (vkCreateDescriptorSetLayout(vk_ctx->device, &layoutInfo, nullptr, &vk_ctx->camera_descriptor_set_layout) != VK_SUCCESS)
    {
        exit_with_error("failed to create camera descriptor set layout!");
    }
}

static void
camera_descriptor_set_create(Context* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    Arena* arena = scratch.arena;

    Buffer<VkDescriptorSetLayout> layouts = buffer_alloc<VkDescriptorSetLayout>(arena, MAX_FRAMES_IN_FLIGHT);

    for (U32 i = 0; i < layouts.size; i++)
    {
        layouts.data[i] = vk_ctx->camera_descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vk_ctx->descriptor_pool;
    allocInfo.descriptorSetCount = layouts.size;
    allocInfo.pSetLayouts = layouts.data;

    if (vkAllocateDescriptorSets(vk_ctx->device, &allocInfo, vk_ctx->camera_descriptor_sets) != VK_SUCCESS)
    {
        exit_with_error("CameraDescriptorSetCreate: failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = vk_ctx->camera_buffer_alloc_mapped[i].buffer_alloc.buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(CameraUniformBuffer);

        VkWriteDescriptorSet uniform_buffer_desc{};
        uniform_buffer_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniform_buffer_desc.dstSet = vk_ctx->camera_descriptor_sets[i];
        uniform_buffer_desc.dstBinding = 0;
        uniform_buffer_desc.dstArrayElement = 0;
        uniform_buffer_desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_buffer_desc.descriptorCount = 1;
        uniform_buffer_desc.pBufferInfo = &buffer_info;
        uniform_buffer_desc.pImageInfo = nullptr;
        uniform_buffer_desc.pTexelBufferView = nullptr;

        VkWriteDescriptorSet descriptors[] = {uniform_buffer_desc};

        vkUpdateDescriptorSets(vk_ctx->device, ArrayCount(descriptors), descriptors, 0, nullptr);
    }

    ScratchEnd(scratch);
}

g_internal DescriptorSetInfo
descriptor_set_uniform_buffer(VkDevice device, VkDescriptorPool desc_pool, void* data)
{
    UniformBufferDescriptor* buffer_desc = (UniformBufferDescriptor*)data;

    VkDescriptorSetLayout desc_set_layout = descriptor_set_layout_create(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}});

    VkDescriptorSet desc_set = descriptor_set_alloc(device, desc_pool, {desc_set_layout});

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer_desc->uniform_buffer;
    buffer_info.offset = 0;
    buffer_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet uniform_buffer_write{};
    uniform_buffer_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uniform_buffer_write.dstSet = desc_set;
    uniform_buffer_write.dstBinding = 0;
    uniform_buffer_write.dstArrayElement = 0;
    uniform_buffer_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniform_buffer_write.descriptorCount = 1;
    uniform_buffer_write.pBufferInfo = &buffer_info;

    descriptor_set_update(device, {uniform_buffer_write});

    return DescriptorSetInfo(desc_set, desc_set_layout);
}

g_internal DescriptorSetInfo
descriptor_set_storage_buffers(VkDevice device, VkDescriptorPool desc_pool, void* data)
{
    StorageBufferDescriptor* buffer_desc = (StorageBufferDescriptor*)data;

    VkDescriptorSetLayout desc_set_layout = descriptor_set_layout_create(
        device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}});

    VkDescriptorSet desc_set = descriptor_set_alloc(device, desc_pool, {desc_set_layout});

    VkDescriptorBufferInfo vertex_buffer_info{};
    vertex_buffer_info.buffer = buffer_desc->vertex_buffer;
    vertex_buffer_info.offset = 0;
    vertex_buffer_info.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo index_buffer_info{};
    index_buffer_info.buffer = buffer_desc->index_buffer;
    index_buffer_info.offset = 0;
    index_buffer_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet vertex_write{};
    vertex_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vertex_write.dstSet = desc_set;
    vertex_write.dstBinding = 0;
    vertex_write.dstArrayElement = 0;
    vertex_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertex_write.descriptorCount = 1;
    vertex_write.pBufferInfo = &vertex_buffer_info;

    VkWriteDescriptorSet index_write{};
    index_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    index_write.dstSet = desc_set;
    index_write.dstBinding = 1;
    index_write.dstArrayElement = 0;
    index_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    index_write.descriptorCount = 1;
    index_write.pBufferInfo = &index_buffer_info;

    descriptor_set_update(device, {vertex_write, index_write});

    return DescriptorSetInfo(desc_set, desc_set_layout);
}

static void
camera_cleanup(Context* vk_ctx)
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        buffer_mapped_destroy(&vk_ctx->camera_buffer_alloc_mapped[i]);
    }

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->camera_descriptor_set_layout, NULL);
}

// ~mgj: Rendering

static void
pipeline_destroy(Pipeline* pipeline)
{
    Context* vk_ctx = ctx_get();
    vkDestroyPipeline(vk_ctx->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx->device, pipeline->pipeline_layout, NULL);
}

static void
model_3d_bucket_add(BufferAllocation* vertex_buffer_allocation, BufferAllocation* index_buffer_allocation, render::Handle tex_handle, render::Handle overlay_tex_handle, B32 overlay_enabled,
                    Vec2F32 overlay_translation, Vec2F32 overlay_scale, B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count, U32 colormap_idx)
{
    Context* vk_ctx = ctx_get();
    RenderFrame* render_frame = vk_ctx->render_frame;

    render::AssetItem<TextureHandle>* base_tex = asset_manager_texture_item_get(tex_handle);
    render::AssetItem<TextureHandle>* overlay_tex = asset_manager_texture_item_get(overlay_tex_handle);
    if (base_tex && overlay_tex)
    {
        Model3dPushConstants push_constants = {};
        push_constants.tex_idx = base_tex->item.descriptor_set_idx;
        push_constants.colormap_idx = colormap_idx;
        push_constants.overlay_tex_idx = overlay_tex->item.descriptor_set_idx;
        push_constants.overlay_enabled = overlay_enabled;
        push_constants.overlay_translation_x = overlay_translation.x;
        push_constants.overlay_translation_y = overlay_translation.y;
        push_constants.overlay_scale_x = overlay_scale.x;
        push_constants.overlay_scale_y = overlay_scale.y;

        Model3DNode* node = PushStruct(vk_ctx->render_frame_arena, Model3DNode);
        node->vertex_alloc = *vertex_buffer_allocation;
        node->index_alloc = *index_buffer_allocation;
        node->push_constants = push_constants;
        node->index_count = index_count;
        node->index_buffer_offset = index_buffer_offset;
        node->depth_write_per_draw_enabled = depth_write_per_draw_call_only;

        SLLQueuePush(render_frame->model_3D_list.first, render_frame->model_3D_list.last, node);
    }
}

static void
blend_3d_bucket_add(BufferAllocation* vertex_buffer_allocation, BufferAllocation* index_buffer_allocation, render::Handle texture_handle, render::Handle colormap_handle)
{
    Context* vk_ctx = ctx_get();
    RenderFrame* render_frame = vk_ctx->render_frame;

    Blend3DNode* node = PushStruct(vk_ctx->render_frame_arena, Blend3DNode);
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;

    render::AssetItem<TextureHandle>* base_tex = asset_manager_texture_item_get(texture_handle);
    render::AssetItem<TextureHandle>* colormap_tex = asset_manager_texture_item_get(colormap_handle);
    Assert(base_tex);
    Assert(colormap_tex);
    if (!base_tex || !colormap_tex)
    {
        return;
    }

    node->push_constants = {.texture_index = base_tex->item.descriptor_set_idx, .colormap_index = colormap_tex->item.descriptor_set_idx};

    SLLQueuePush(render_frame->blend_3d_list.first, render_frame->blend_3d_list.last, node);
}

enum WriteType
{
    WriteType_Color,
    WriteType_Depth,
    WriteType_Count
};

g_internal void
draw_indexed_separate_depth_and_color_calls(VkCommandBuffer cmd_buffer, U32 index_offset, U32 index_count)
{
    VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
    VkBool32 color_write_disabled[4] = {};
    for (WriteType write_type = (WriteType)0; write_type < WriteType_Count; write_type = (WriteType)(write_type + 1))
    {
        if (write_type == WriteType_Color)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_FALSE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled), color_write_enabled);
        }
        else if (write_type == WriteType_Depth)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_disabled), color_write_disabled);
        }

        vkCmdDrawIndexed(cmd_buffer, index_count, 1, index_offset, 0, 0);
    }
}

static void
model_3d_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "model_3d_rendering");

    Pipeline* model_3D_pipeline = &vk_ctx->model_3D_pipeline;
    RenderFrame* render_frame = vk_ctx->render_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, model_3D_pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame], vk_ctx->bindless_descriptor_set};

    VkDeviceSize offsets[] = {0};
    for (Model3DNode* node = render_frame->model_3D_list.first; node; node = node->next)
    {
        vkCmdPushConstants(cmd_buffer, model_3D_pipeline->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Model3dPushConstants), &node->push_constants);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, model_3D_pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
        if (node->depth_write_per_draw_enabled)
        {
            draw_indexed_separate_depth_and_color_calls(cmd_buffer, node->index_buffer_offset, node->index_count);
        }
        else
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled), color_write_enabled);
            vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
        }
    }
}

static void
blend_3d_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "blend_3d_rendering");

    Pipeline* blend_3d_pipeline = &vk_ctx->blend_3d_pipeline;
    RenderFrame* render_frame = vk_ctx->render_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_3d_pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame], vk_ctx->bindless_descriptor_set};

    VkDeviceSize offsets[] = {0};
    for (Blend3DNode* node = render_frame->blend_3d_list.first; node; node = node->next)
    {
        vkCmdPushConstants(cmd_buffer, blend_3d_pipeline->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Blend3dPushConstants), &node->push_constants);
        U32 index_count = node->index_alloc.size / sizeof(U32);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_3d_pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);

        draw_indexed_separate_depth_and_color_calls(cmd_buffer, 0, index_count);
    }
}

static void
swapchain_image_barrier_between_rendering(VkCommandBuffer cmd_buffer, VkImage swapchain_image)
{
    VkImageMemoryBarrier2 imgui_render_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .image = swapchain_image,
                                                  .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};
    VkDependencyInfo renderpass_dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imgui_render_barrier};
    vkCmdPipelineBarrier2(cmd_buffer, &renderpass_dep_info);
}

static void
command_buffer_record(U32 image_index, U32 current_frame, ui::Camera* camera, Vec2S64 mouse_cursor_pos)
{
    prof_scope_marker;
    Temp scratch = ScratchBegin(0, 0);

    Context* vk_ctx = ctx_get();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    SwapchainResources* swapchain_resource = vk_ctx->swapchain_resources;

    // object id color attachment image
    ImageResource* object_id_image_resource = &swapchain_resource->object_id_image_resources.data[image_index];
    ImageAllocation* object_id_image_alloc = &object_id_image_resource->image_alloc;
    VkImageView object_id_image_view = object_id_image_resource->image_view_resource.image_view;
    VkImage object_id_image = object_id_image_alloc->image;

    // object id resolve image
    ImageResource* object_id_image_resolve_resource = &swapchain_resource->object_id_image_resolve_resources.data[image_index];
    VkImageView object_id_image_resolve_view = object_id_image_resolve_resource->image_view_resource.image_view;
    VkImage object_id_resolve_image = object_id_image_resolve_resource->image_alloc.image;

    VkImageView swapchain_image_view = swapchain_resource->image_resources.data[image_index].image_view_resource.image_view;

    VkImage swapchain_image = swapchain_resource->image_resources.data[image_index].image;
    // ~mgj: Render scope (Tracy Profiler documentation says this is necessary)
    {
        VkCommandBuffer current_cmd_buf = vk_ctx->command_buffers.data[current_frame];
        VkResult result_vk = vkBeginCommandBuffer(current_cmd_buf, &beginInfo);
        if (result_vk)
        {
            exit_with_error("failed to begin recording command buffer!");
        }
        VkImageMemoryBarrier2 pre_render_swapchain_barrier{};
        pre_render_swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        pre_render_swapchain_barrier.pNext = nullptr;
        pre_render_swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        pre_render_swapchain_barrier.srcAccessMask = VK_ACCESS_2_NONE;
        pre_render_swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        pre_render_swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        pre_render_swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pre_render_swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        pre_render_swapchain_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_render_swapchain_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_render_swapchain_barrier.image = swapchain_image;
        pre_render_swapchain_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

        VkImageMemoryBarrier2 pre_render_object_id_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                           .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                                           .srcAccessMask = VK_ACCESS_2_NONE,
                                                           .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                           .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                           .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                           .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                           .image = object_id_image,
                                                           .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_object_id_resolve_image_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = object_id_resolve_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_barriers[] = {pre_render_object_id_barrier, pre_render_swapchain_barrier, pre_render_object_id_resolve_image_barrier};
        VkDependencyInfo pre_render_transition_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = ArrayCount(pre_render_barriers), .pImageMemoryBarriers = pre_render_barriers};

        vkCmdPipelineBarrier2(current_cmd_buf, &pre_render_transition_info);
        {
            TracyVkZone(vk_ctx->tracy_ctx[current_frame], current_cmd_buf, "Render"); // NOLINT

            VkExtent2D swapchain_extent = swapchain_resource->swapchain_extent;
            VkImageView color_image_view = swapchain_resource->color_image_resource.image_view_resource.image_view;
            VkImageView depth_image_view = swapchain_resource->depth_image_resource.image_view_resource.image_view;

            VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            VkClearDepthStencilValue clear_depth = {1.0f, 0};

            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue = {.color = clear_color};

            VkRenderingAttachmentInfo object_id_attachment{};
            object_id_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            object_id_attachment.imageView = object_id_image_view;
            object_id_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            object_id_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            object_id_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            object_id_attachment.clearValue = {.color = clear_color};

            if (vk_ctx->msaa_samples == VK_SAMPLE_COUNT_1_BIT)
            {
                color_attachment.imageView = swapchain_image_view;
                object_id_attachment.imageView = object_id_image_resolve_view;
            }
            else
            {
                color_attachment.imageView = color_image_view;
                color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                color_attachment.resolveImageView = swapchain_image_view;
                color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                object_id_attachment.imageView = object_id_image_view;
                object_id_attachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                object_id_attachment.resolveImageView = object_id_image_resolve_view;
                object_id_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = depth_image_view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth_attachment.clearValue = {.depthStencil = clear_depth};

            VkRenderingAttachmentInfo color_attachments[] = {color_attachment, object_id_attachment};
            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = swapchain_extent;
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = ArrayCount(color_attachments);
            rendering_info.pColorAttachments = color_attachments;
            rendering_info.pDepthAttachment = &depth_attachment;

            camera_uniform_buffer_update(vk_ctx, camera, Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width, (F32)vk_ctx->swapchain_resources->swapchain_extent.height}, current_frame);

            VkDebugUtilsLabelEXT debug_label{};
            debug_label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;

            vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame] =
                buffer_alloc_create_or_resize(vk_ctx->render_frame->car_instance_render_list.total_instance_buffer_byte_count, vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame],
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "model_3D instance buffer");

            // ~mgj: Compute shaders
            debug_label.pLabelName = "Road Intersection Compute";
            CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(current_cmd_buf, &debug_label);
            road_intersection_compute();
            CMD_END_DEBUG_UTILS_LABEL_EXT(current_cmd_buf);

            debug_label.pLabelName = "Car Instance Compute";
            CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(current_cmd_buf, &debug_label);
            car_instance_compute();
            CMD_END_DEBUG_UTILS_LABEL_EXT(current_cmd_buf);

            render::AssetItem<BufferHandle>* model_3D_instance_buffer = vulkan::asset_manager_buffer_item_get(vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame]);
            if (model_3D_instance_buffer)
            {
                BufferAllocation instance_buffer_alloc = model_3D_instance_buffer->item.buffer_alloc;
                VkBuffer instance_buffer = instance_buffer_alloc.buffer;
                for (CarInstanceRenderNode* node = vk_ctx->render_frame->car_instance_render_list.list.first; node; node = node->next)
                {
                    // All compute shader from car_instance_compute() should have finished
                    VkBufferMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                                      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                      .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                      .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                                                      .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .buffer = instance_buffer,
                                                      .offset = node->instance_buffer_offset,
                                                      .size = node->instance_buffer_info.buffer.size};
                    VkDependencyInfo dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
                    vkCmdPipelineBarrier2(current_cmd_buf, &dep_info);
                }
            }

            // ~mgj: Render pass
            vkCmdBeginRendering(current_cmd_buf, &rendering_info);

            debug_label.pLabelName = "Car Instance Rendering";
            CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(current_cmd_buf, &debug_label);
            car_instance_rendering();
            CMD_END_DEBUG_UTILS_LABEL_EXT(current_cmd_buf);

            debug_label.pLabelName = "Model 3D Rendering";
            CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(current_cmd_buf, &debug_label);
            model_3d_rendering();
            CMD_END_DEBUG_UTILS_LABEL_EXT(current_cmd_buf);

            blend_3d_rendering();

            vkCmdEndRendering(current_cmd_buf);

            swapchain_image_barrier_between_rendering(current_cmd_buf, swapchain_image);

            VkRenderingAttachmentInfo imgui_color_attachment{};
            imgui_color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            imgui_color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgui_color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            imgui_color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            imgui_color_attachment.clearValue = {.color = clear_color};
            imgui_color_attachment.imageView = swapchain_image_view;

            VkRenderingInfo imgui_rendering_info{};
            imgui_rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            imgui_rendering_info.renderArea.offset = {0, 0};
            imgui_rendering_info.renderArea.extent = swapchain_extent;
            imgui_rendering_info.layerCount = 1;
            imgui_rendering_info.colorAttachmentCount = 1;
            imgui_rendering_info.pColorAttachments = &imgui_color_attachment;

            vkCmdBeginRendering(current_cmd_buf, &imgui_rendering_info);

            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();
            const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
            if (!is_minimized)
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, current_cmd_buf);
            }

            vkCmdEndRendering(current_cmd_buf);

            VkImageMemoryBarrier2 present_barrier{};
            present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            present_barrier.pNext = nullptr;
            present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            present_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            present_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            present_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            present_barrier.image = swapchain_image;
            present_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
            VkImageMemoryBarrier2 object_id_read_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                            .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                            .image = object_id_resolve_image,
                                                            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

            VkImageMemoryBarrier2 post_render_barriers[] = {present_barrier, object_id_read_barrier};
            VkDependencyInfo layout_transition_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = ArrayCount(post_render_barriers), .pImageMemoryBarriers = post_render_barriers};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_transition_info);

            Vec2S64 mouse_position_screen_coords = mouse_cursor_pos;
            if (mouse_position_screen_coords.x > 0 && mouse_position_screen_coords.y > 0 && mouse_position_screen_coords.x < vk_ctx->swapchain_resources->swapchain_extent.width &&
                mouse_position_screen_coords.y < vk_ctx->swapchain_resources->swapchain_extent.height)
            {
                VkBufferMemoryBarrier2 readback_barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                                           .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                           .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                           .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                           .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                           .buffer = swapchain_resource->object_id_buffer_readback[current_frame].buffer_alloc.buffer,
                                                           .offset = 0,
                                                           .size = VK_WHOLE_SIZE};
                VkDependencyInfo readback_dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &readback_barrier};
                vkCmdPipelineBarrier2(current_cmd_buf, &readback_dep_info);

                VkBufferImageCopy buffer_image_copy[] = {{
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .imageOffset = {(S32)mouse_position_screen_coords.x, (S32)mouse_position_screen_coords.y, 0},
                    .imageExtent = {1, 1, 1},
                }};
                vkCmdCopyImageToBuffer(current_cmd_buf, object_id_resolve_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_resource->object_id_buffer_readback[current_frame].buffer_alloc.buffer,
                                       ArrayCount(buffer_image_copy), buffer_image_copy);

                Assert(vk_ctx->object_id_format == VK_FORMAT_R32G32_UINT);
                U64* object_id = (U64*)swapchain_resource->object_id_buffer_readback[current_frame].mapped_ptr;
                vk_ctx->hovered_object_id = *object_id;
            }

            VkImageMemoryBarrier2 object_id_reset_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                             .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                             .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                             .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                                                             .dstAccessMask = VK_ACCESS_2_NONE,
                                                             .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                             .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                             .image = object_id_resolve_image,
                                                             .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

            VkDependencyInfo layout_reset_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &object_id_reset_barrier};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_reset_transition_info);
        }

        TracyVkCollect(vk_ctx->tracy_ctx[current_frame], current_cmd_buf);

        VkResult result = vkEndCommandBuffer(current_cmd_buf);
        if (result)
        {
            exit_with_error("failed to record command buffer!");
        }
    }
    ScratchEnd(scratch);
}

static void
profile_buffers_create(Context* vk_ctx)
{
#ifdef TRACY_ENABLE
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        vk_ctx->tracy_ctx[i] = TracyVkContext(vk_ctx->physical_device, vk_ctx->device, vk_ctx->graphics_queue, vk_ctx->command_buffers.data[i]);
    }
#endif
}

static void
profile_buffers_destroy(Context* vk_ctx)
{
#ifdef TRACY_ENABLE
    vkQueueWaitIdle(vk_ctx->graphics_queue);
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        TracyVkDestroy(vk_ctx->tracy_ctx[i]);
    }
#endif
}

} // namespace vulkan
