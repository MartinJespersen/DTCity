namespace ui
{

static void
camera_init(Camera* camera, Vec2F32 start_pos)
{
    camera->move_sensitivity = 20.0f;
    camera->fov = 45.0;
    camera->position = glm::vec3(0.0f, 0.0f, 150.0f);
    camera->yaw = 0.0f;
    camera->pitch = 88.0f;
    camera->view_dir = ui_direction_normal_from_euler_angles(camera->yaw, camera->pitch);
    camera->world_offset = {start_pos.x, start_pos.y};
}

static void
camera_update(Camera* camera, io_IO* input, F32 time, Vec2U32 extent)
{
    F32 mouse_sensitivity = 0.1f;
    F32 scroll_sensitivity = 0.2f;
    if (input->mouse_left_clicked & input->is_cursor_inside_win & input->is_window_focused)
    {
        F32 mouse_delta_x = input->mouse_pos_cur.x - camera->mouse_pos_last.x;
        F32 mouse_delta_y = camera->mouse_pos_last.y - input->mouse_pos_cur.y;
        camera->yaw -= mouse_delta_x * mouse_sensitivity;
        camera->pitch -= mouse_delta_y * mouse_sensitivity;

        if (camera->pitch > 89.0f)
            camera->pitch = 89.0f;
        if (camera->pitch < -89.0f)
            camera->pitch = -89.0f;
        camera->view_dir = ui_direction_normal_from_euler_angles(camera->yaw, camera->pitch);
    }
    camera->mouse_pos_last.x = input->mouse_pos_cur.x;
    camera->mouse_pos_last.y = input->mouse_pos_cur.y;

    camera->fov -=
        (F32)input->scroll_y.load(std::memory_order::memory_order_seq_cst) * scroll_sensitivity;
    if (camera->fov < 1.0f)
        camera->fov = 1.0f;
    if (camera->fov > 45.0f)
        camera->fov = 45.0f;

    glm::vec3 camera_up = glm::vec3(0.0f, 0.0f, 1.0f);

    F32 move_y = (input->w_btn_clicked + (-input->s_btn_clicked)) * time * camera->move_sensitivity;
    F32 move_x = (-input->a_btn_clicked + (input->d_btn_clicked)) * time * camera->move_sensitivity;
    glm::vec3 x_view_norm = glm::normalize(glm::cross(camera->view_dir, camera_up));

    camera->position -= camera->view_dir * move_y;
    camera->position -= move_x * x_view_norm;

    camera->view_matrix =
        glm::lookAt(camera->position, camera->position - camera->view_dir, camera_up);

    camera->projection_matrix = glm::perspective(
        glm::radians(camera->fov), (F32)((F32)extent.x / (F32)extent.y), 0.1f, 1000.0f);
    camera->projection_matrix[1][1] *= -1.0f;
    input->scroll_y.store(0.0, std::memory_order::memory_order_seq_cst);
}
} // namespace ui
