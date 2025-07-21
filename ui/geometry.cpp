namespace ui
{

glm::vec3
DirectionNormalFromEulerAngles(F32 yaw, F32 pitch)
{
    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

    return glm::normalize(direction);
}

static void
CameraInit(Camera* camera)
{
    camera->zoom_sensitivity = 20.0f;
    camera->fov = 45.0;
    camera->position = glm::vec3(0.0f, 150.0f, 1.0f);
    camera->yaw = 0.0f;
    camera->pitch = -88.0f;
    camera->view_dir = DirectionNormalFromEulerAngles(camera->yaw, camera->pitch);
}

static void
CameraUpdate(Camera* camera, IO* input, DT_Time* time, VkExtent2D extent)
{
    F32 mouse_sensitivity = 0.1f;

    if (input->mouse_left_clicked & input->is_cursor_inside_win & input->is_window_focused)
    {
        F32 mouse_delta_x = input->mouse_pos_cur.x - camera->mouse_pos_last.x;
        F32 mouse_delta_y = camera->mouse_pos_last.y - input->mouse_pos_cur.y;
        camera->yaw += mouse_delta_x * mouse_sensitivity;
        camera->pitch += mouse_delta_y * mouse_sensitivity;

        if (camera->pitch > 89.0f)
            camera->pitch = 89.0f;
        if (camera->pitch < -89.0f)
            camera->pitch = -89.0f;

        camera->view_dir = DirectionNormalFromEulerAngles(camera->yaw, camera->pitch);
    }
    camera->mouse_pos_last.x = input->mouse_pos_cur.x;
    camera->mouse_pos_last.y = input->mouse_pos_cur.y;

    camera->fov -= (F32)input->scroll_y;
    if (camera->fov < 1.0f)
        camera->fov = 1.0f;
    if (camera->fov > 45.0f)
        camera->fov = 45.0f;

    glm::vec3 camera_up = glm::vec3(0.0f, 1.0f, 0.0f);

    F32 zoom_y = (input->w_btn_clicked + (-input->s_btn_clicked)) * time->delta_time_sec *
                 camera->zoom_sensitivity;
    F32 zoom_x = (-input->a_btn_clicked + (input->d_btn_clicked)) * time->delta_time_sec *
                 camera->zoom_sensitivity;
    glm::vec3 x_view_norm = glm::normalize(glm::cross(camera->view_dir, camera_up));

    camera->position += camera->view_dir * zoom_y;
    camera->position += zoom_x * x_view_norm;
    // camera_pos + camera_front
    camera->view_matrix =
        glm::lookAt(camera->position, camera->position + camera->view_dir, camera_up);
    camera->projection_matrix = glm::perspective(
        glm::radians(camera->fov), (F32)((F32)extent.width / (F32)extent.height), 0.1f, 300.0f);
    camera->projection_matrix[1][1] *= -1.0f;
}

} // namespace ui
