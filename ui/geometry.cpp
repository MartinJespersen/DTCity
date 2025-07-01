internal void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix)
{
    out_frustum->planes[LEFT].x = matrix[0].w + matrix[0].x;
    out_frustum->planes[LEFT].y = matrix[1].w + matrix[1].x;
    out_frustum->planes[LEFT].z = matrix[2].w + matrix[2].x;
    out_frustum->planes[LEFT].w = matrix[3].w + matrix[3].x;

    out_frustum->planes[RIGHT].x = matrix[0].w - matrix[0].x;
    out_frustum->planes[RIGHT].y = matrix[1].w - matrix[1].x;
    out_frustum->planes[RIGHT].z = matrix[2].w - matrix[2].x;
    out_frustum->planes[RIGHT].w = matrix[3].w - matrix[3].x;

    out_frustum->planes[TOP].x = matrix[0].w - matrix[0].y;
    out_frustum->planes[TOP].y = matrix[1].w - matrix[1].y;
    out_frustum->planes[TOP].z = matrix[2].w - matrix[2].y;
    out_frustum->planes[TOP].w = matrix[3].w - matrix[3].y;

    out_frustum->planes[BOTTOM].x = matrix[0].w + matrix[0].y;
    out_frustum->planes[BOTTOM].y = matrix[1].w + matrix[1].y;
    out_frustum->planes[BOTTOM].z = matrix[2].w + matrix[2].y;
    out_frustum->planes[BOTTOM].w = matrix[3].w + matrix[3].y;

    out_frustum->planes[BACK].x = matrix[0].w + matrix[0].z;
    out_frustum->planes[BACK].y = matrix[1].w + matrix[1].z;
    out_frustum->planes[BACK].z = matrix[2].w + matrix[2].z;
    out_frustum->planes[BACK].w = matrix[3].w + matrix[3].z;

    out_frustum->planes[FRONT].x = matrix[0].w - matrix[0].z;
    out_frustum->planes[FRONT].y = matrix[1].w - matrix[1].z;
    out_frustum->planes[FRONT].z = matrix[2].w - matrix[2].z;
    out_frustum->planes[FRONT].w = matrix[3].w - matrix[3].z;

    for (size_t i = 0; i < ArrayCount(out_frustum->planes); i++)
    {
        float length = sqrtf(out_frustum->planes[i].x * out_frustum->planes[i].x +
                             out_frustum->planes[i].y * out_frustum->planes[i].y +
                             out_frustum->planes[i].z * out_frustum->planes[i].z);
        out_frustum->planes[i] /= length;
    }
}

internal void
CameraUpdate(Context* ctx)
{
    IO* input = ctx->io;
    VulkanContext* vk_ctx = ctx->vulkanContext;

    ctx->view_matrix = glm::lookAt(glm::vec3(0.0f, 50.0f, 0.1f), glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(0.0f, 1.0f, 0.0f));
    ctx->projection_matrix = glm::perspective(
        glm::radians(45.0f),
        (F32)((F32)vk_ctx->swapchain_extent.width / (F32)vk_ctx->swapchain_extent.height), 0.1f,
        100.0f);
    ctx->projection_matrix[1][1] *= -1.0f;
}
