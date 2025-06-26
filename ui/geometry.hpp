#pragma once

enum PlaneType
{
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    BACK,
    FRONT,
    PlaneType_Count
};

struct Frustum
{
    glm::vec4 planes[PlaneType_Count];
};

internal void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix);
