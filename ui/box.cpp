root_function void
BoxIndexBufferCreate(BoxContext* box_context, VkPhysicalDevice physicalDevice, VkDevice device,
                     VkCommandPool commandPool, VkQueue graphicsQueue, u16_Buffer indices)
{
    VkDeviceSize bufferSize = sizeof(indices.data[0]) * indices.size;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    BufferCreate(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    MemoryCopy(data, indices.data, (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    BufferCreate(physicalDevice, device, bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, box_context->indexBuffer,
                 box_context->indexMemoryBuffer);

    copyBuffer(device, commandPool, graphicsQueue, stagingBuffer, box_context->indexBuffer,
               bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

root_function void
BoxRenderPassBegin(BoxContext* box_context, VulkanContext* vulkanContext, u32 imageIndex,
                   u32 currentFrame)
{
    VkExtent2D swapChainExtent = vulkanContext->swapChainExtent;
    VkCommandBuffer commandBuffer = vulkanContext->commandBuffers.data[currentFrame];
    Vulkan_PushConstantInfo pushContextInfo = vulkanContext->resolutionInfo;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vulkanContext->boxRenderPass;
    renderPassInfo.framebuffer = vulkanContext->swapChainFramebuffers.data[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    const u32 clearValueCount = 2;
    VkClearValue clearValues[clearValueCount] = {0};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = clearValueCount;
    renderPassInfo.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      box_context->graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {box_context->instBuffer};
    VkDeviceSize offsets[] = {0};
    f32 resolutionData[2] = {(f32)swapChainExtent.width, (f32)swapChainExtent.height};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer, box_context->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdPushConstants(commandBuffer, box_context->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       pushContextInfo.offset, pushContextInfo.size, resolutionData);

    vkCmdDrawIndexed(commandBuffer, 6, static_cast<uint32_t>(box_context->numInstances), 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
}

root_function void
BoxCleanup(BoxContext* box_context, VkDevice device)
{
    vkDestroyBuffer(device, box_context->indexBuffer, nullptr);
    vkFreeMemory(device, box_context->indexMemoryBuffer, nullptr);

    vkDestroyBuffer(device, box_context->instBuffer, nullptr);
    vkFreeMemory(device, box_context->instMemoryBuffer, nullptr);

    vkDestroyPipeline(device, box_context->graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, box_context->pipelineLayout, nullptr);
}

root_function void
InstanceBufferFillFromBoxes(BoxContext* box_context, VkPhysicalDevice physicalDevice,
                            VkDevice device)
{
    VkDeviceSize bufferSize = sizeof(Vulkan_BoxInstance) * box_context->numInstances;
    if (bufferSize > box_context->instBufferSize)
    {
        vkDestroyBuffer(device, box_context->instBuffer, nullptr);
        vkFreeMemory(device, box_context->instMemoryBuffer, nullptr);

        BufferCreate(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     box_context->instBuffer, box_context->instMemoryBuffer);
    }

    // TODO: Consider using vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges instead of
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT for performance.

    if (bufferSize != 0)
    {
        void* data;
        vkMapMemory(device, box_context->instMemoryBuffer, 0, bufferSize, 0, &data);
        MemoryCopy(data, box_context->boxInstances.data, (size_t)bufferSize);
        vkUnmapMemory(device, box_context->instMemoryBuffer);
    }

    box_context->instBufferSize = bufferSize;
}

root_function u64
InstanceBufferFromBoxes(Box* first, Array<Vulkan_BoxInstance> outBuffer)
{
    u64 numInstances = 0;
    for (Box* box = first; !IsNull(box); box = box->next)
    {
        outBuffer[numInstances].pos0 = Vec2(box->pos0.x, box->pos0.y);
        outBuffer[numInstances].pos1 = Vec2(box->pos1.x, box->pos1.y);
        outBuffer[numInstances].color = {box->color.data[0], box->color.data[1], box->color.data[2],
                                         box->color.data[3]};
        outBuffer[numInstances].borderThickness = box->borderThickness;
        outBuffer[numInstances].cornerRadius = box->cornerRadius;
        outBuffer[numInstances].softness = box->softness;
        outBuffer[numInstances].attributes = box->attributes;
        numInstances++;
    }
    return numInstances;
}

root_function void
BoxFrameReset(Arena* arena, BoxContext* box_context)
{
    box_context->boxInstances =
        ArrayAlloc<Vulkan_BoxInstance>(arena, box_context->MAX_BOX_INSTANCES);
    box_context->boxQueue = {0};
}