

glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch)
{
    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

    return glm::normalize(direction);
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
        return (true);
    }

    /* Are the line parallel */
    if (AbsF64(denom) < EPS)
    {
        *x = 0;
        *y = 0;
        return (false);
    }

    /* Is the intersection along the the segments */
    mua = numera / denom;
    mub = numerb / denom;
    if (mua < 0 || mua > 1 || mub < 0 || mub > 1)
    {
        *x = 0;
        *y = 0;
        return (false);
    }
    *x = x1 + mua * (x2 - x1);
    *y = y1 + mua * (y2 - y1);
    return (true);
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
