

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
ui_camera_init(Camera* camera)
{
    camera->zoom_sensitivity = 20.0f;
    camera->fov = 45.0;
    camera->position = glm::vec3(0.0f, 150.0f, 1.0f);
    camera->yaw = 0.0f;
    camera->pitch = -88.0f;
    camera->view_dir = DirectionNormalFromEulerAngles(camera->yaw, camera->pitch);
}

static void
ui_camera_update(Camera* camera, IO* input, F32 time, Vec2U32 extent)
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

    F32 zoom_y = (input->w_btn_clicked + (-input->s_btn_clicked)) * time * camera->zoom_sensitivity;
    F32 zoom_x = (-input->a_btn_clicked + (input->d_btn_clicked)) * time * camera->zoom_sensitivity;
    glm::vec3 x_view_norm = glm::normalize(glm::cross(camera->view_dir, camera_up));

    camera->position += camera->view_dir * zoom_y;
    camera->position += zoom_x * x_view_norm;
    // camera_pos + camera_front
    camera->view_matrix =
        glm::lookAt(camera->position, camera->position + camera->view_dir, camera_up);
    camera->projection_matrix = glm::perspective(
        glm::radians(camera->fov), (F32)((F32)extent.x / (F32)extent.y), 0.1f, 1000.0f);
    camera->projection_matrix[1][1] *= -1.0f;
}

static B32
ui_line_intersect(F64 x1, F64 y1, F64 x2, F64 y2, F64 x3, F64 y3, F64 x4, F64 y4, F64* x, F64* y)
{
    F64 EPS = 0.001;
    F64 mua, mub;
    F64 denom, numera, numerb;

    denom = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);
    numera = (x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3);
    numerb = (x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3);

    /* Are the line coincident? */
    if (AbsF64(numera) < EPS && AbsF64(numerb) < EPS && AbsF64(denom) < EPS)
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return (TRUE);
    }

    /* Are the line parallel */
    if (AbsF64(denom) < EPS)
    {
        *x = 0;
        *y = 0;
        return (FALSE);
    }

    /* Is the intersection along the the segments */
    mua = numera / denom;
    mub = numerb / denom;
    if (mua < 0 || mua > 1 || mub < 0 || mub > 1)
    {
        *x = 0;
        *y = 0;
        return (FALSE);
    }
    *x = x1 + mua * (x2 - x1);
    *y = y1 + mua * (y2 - y1);
    return (TRUE);
}

static B32
ui_line_intersect_2f32(Vec2F32 v0, Vec2F32 v1, Vec2F32 v2, Vec2F32 v3, Vec2F32* res)
{
    Vec2F64 res_f64 = {};

    B32 does_intersect =
        ui_line_intersect(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, &res_f64.x, &res_f64.y);

    res->x = (F32)res_f64.x;
    res->y = (F32)res_f64.y;

    return does_intersect;
}
