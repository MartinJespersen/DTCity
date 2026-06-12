namespace ui
{

g_internal void
camera_init(Arena* arena, Camera* camera)
{
    camera->move_sensitivity = 300.0f;
    camera->fov = 45.0;
    camera->position = glm::vec3(0.0f, 0.0f, 1000.0f);
    camera->yaw = 0.0f;
    camera->pitch = -60.0f; // Look downward at the ground
    camera->view_dir = ui_direction_normal_from_euler_angles(camera->yaw, camera->pitch);

    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer(render::thread_cmd_buffer_end(thread_ctx));
    render::BufferType buffer_type = render::BufferType_Uniform;
    for (U32 i = 0; i < ArrayCount(camera->mut_handles); ++i)
    {
        camera->mut_handles[i] = render::mapped_buffer_create<ui::CameraUniformBuffer>(arena, thread_ctx, buffer_type);
    }
}

static void
camera_update(Camera* camera, io::IO* input, F64 time, Vec2S32 extent, U32 current_frame_idx, bool enable)
{
    F32 mouse_sensitivity = 0.1f;
    F32 scroll_sensitivity = 0.2f;
    if (enable)
    {
        if (input->mouse_left_clicked & input->is_cursor_inside_win & input->is_window_focused)
        {
            F32 mouse_delta_x = input->mouse_pos_cur.x - camera->mouse_pos_last.x;
            F32 mouse_delta_y = input->mouse_pos_cur.y - camera->mouse_pos_last.y;
            camera->yaw -= mouse_delta_x * mouse_sensitivity;
            camera->pitch -= mouse_delta_y * mouse_sensitivity;

            if (camera->pitch > 89.0f)
                camera->pitch = 89.0f;
            if (camera->pitch < -89.0f)
                camera->pitch = -89.0f;
            camera->view_dir = ui_direction_normal_from_euler_angles(camera->yaw, camera->pitch);
        }

        camera->fov -= (F32)input->scroll_y.load(std::memory_order_seq_cst) * scroll_sensitivity;
        if (camera->fov < 1.0f)
            camera->fov = 1.0f;
        if (camera->fov > 45.0f)
            camera->fov = 45.0f;

        glm::vec3 camera_up = glm::vec3(0.0f, 0.0f, 1.0f);

        F64 move_y = (input->w_btn_clicked + (-input->s_btn_clicked)) * time * (F64)camera->move_sensitivity;
        F64 move_x = (-input->a_btn_clicked + (input->d_btn_clicked)) * time * (F64)camera->move_sensitivity;
        glm::vec3 x_view_norm = glm::normalize(glm::cross(camera->view_dir, camera_up));

        camera->position += camera->view_dir * (F32)move_y;
        camera->position += (F32)move_x * x_view_norm;
    }

    camera->mouse_pos_last.x = input->mouse_pos_cur.x;
    camera->mouse_pos_last.y = input->mouse_pos_cur.y;
    input->scroll_y.store(0.0, std::memory_order_seq_cst);

    glm::vec3 camera_up = glm::vec3(0.0f, 0.0f, 1.0f);
    camera->view_matrix = glm::lookAt(camera->position, camera->position + camera->view_dir, camera_up);

    F32 aspect_ratio = (F32)Max(extent.x, 1) / (F32)Max(extent.y, 1);
    camera->projection_matrix = glm::perspective(glm::radians(camera->fov), aspect_ratio, 0.1f, 5000.0f);
    camera->projection_matrix[1][1] *= -1.0f;

    Vec2U32 camera_framebuffer_dim = vec_2u32((U32)Max(extent.x, 1), (U32)Max(extent.y, 1));
    render::MappedHandle<ui::CameraUniformBuffer> camera_handle = camera->mut_handles[current_frame_idx];
    ui::_camera_uniform_buffer_update(camera, camera_handle, camera_framebuffer_dim);
}

g_internal void
_camera_uniform_buffer_update(ui::Camera* camera, render::MappedHandle<CameraUniformBuffer> mut_handle, Vec2U32 screen_res)
{
    ScratchScope scratch = ScratchScope(0, 0);
    glm::mat4 transform = camera->projection_matrix * camera->view_matrix;
    ui::CameraUniformBuffer ubo = {};
    _frustum_planes_calculate(&ubo.frustum, transform);
    ubo.viewport_dim.x = screen_res.x;
    ubo.viewport_dim.y = screen_res.y;
    ubo.view = camera->view_matrix;
    ubo.proj = camera->projection_matrix;
    render::mapped_buffer_add(mut_handle, &ubo);
}

g_internal void
_frustum_planes_calculate(Frustum* out_frustum, const glm::mat4 matrix)
{
    out_frustum->planes[PlaneType_Left].x = matrix[0].w + matrix[0].x;
    out_frustum->planes[PlaneType_Left].y = matrix[1].w + matrix[1].x;
    out_frustum->planes[PlaneType_Left].z = matrix[2].w + matrix[2].x;
    out_frustum->planes[PlaneType_Left].w = matrix[3].w + matrix[3].x;

    out_frustum->planes[PlaneType_Right].x = matrix[0].w - matrix[0].x;
    out_frustum->planes[PlaneType_Right].y = matrix[1].w - matrix[1].x;
    out_frustum->planes[PlaneType_Right].z = matrix[2].w - matrix[2].x;
    out_frustum->planes[PlaneType_Right].w = matrix[3].w - matrix[3].x;

    out_frustum->planes[PlaneType_Top].x = matrix[0].w - matrix[0].y;
    out_frustum->planes[PlaneType_Top].y = matrix[1].w - matrix[1].y;
    out_frustum->planes[PlaneType_Top].z = matrix[2].w - matrix[2].y;
    out_frustum->planes[PlaneType_Top].w = matrix[3].w - matrix[3].y;

    out_frustum->planes[PlaneType_Btm].x = matrix[0].w + matrix[0].y;
    out_frustum->planes[PlaneType_Btm].y = matrix[1].w + matrix[1].y;
    out_frustum->planes[PlaneType_Btm].z = matrix[2].w + matrix[2].y;
    out_frustum->planes[PlaneType_Btm].w = matrix[3].w + matrix[3].y;

    out_frustum->planes[PlaneType_Back].x = matrix[0].w + matrix[0].z;
    out_frustum->planes[PlaneType_Back].y = matrix[1].w + matrix[1].z;
    out_frustum->planes[PlaneType_Back].z = matrix[2].w + matrix[2].z;
    out_frustum->planes[PlaneType_Back].w = matrix[3].w + matrix[3].z;

    out_frustum->planes[PlaneType_Front].x = matrix[0].w - matrix[0].z;
    out_frustum->planes[PlaneType_Front].y = matrix[1].w - matrix[1].z;
    out_frustum->planes[PlaneType_Front].z = matrix[2].w - matrix[2].z;
    out_frustum->planes[PlaneType_Front].w = matrix[3].w - matrix[3].z;

    for (size_t i = 0; i < ArrayCount(out_frustum->planes); i++)
    {
        float length = sqrtf(out_frustum->planes[i].x * out_frustum->planes[i].x + out_frustum->planes[i].y * out_frustum->planes[i].y + out_frustum->planes[i].z * out_frustum->planes[i].z);
        out_frustum->planes[i] /= length;
    }
}
} // namespace ui
