static void
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

static void
UI_CameraInit(UI_Camera* camera)
{
    camera->zoom_sensitivity = 10.0f;
    camera->fov = 45.0;
    camera->position = glm::vec3(0.0f, 50.0f, 1.0f);
    camera->view_dir = glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f));
    camera->yaw = 0.0f;
    camera->pitch = -88.0f;
}

static void
UI_CameraUpdate(UI_Camera* camera, IO* input, DT_Time* time, VkExtent2D extent)
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

        glm::vec3 direction;
        direction.x = cos(glm::radians(camera->yaw)) * cos(glm::radians(camera->pitch));
        direction.y = sin(glm::radians(camera->pitch));
        direction.z = sin(glm::radians(camera->yaw)) * cos(glm::radians(camera->pitch));

        camera->view_dir = glm::normalize(direction);
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
        glm::radians(camera->fov), (F32)((F32)extent.width / (F32)extent.height), 0.1f, 150.0f);
    camera->projection_matrix[1][1] *= -1.0f;
}
