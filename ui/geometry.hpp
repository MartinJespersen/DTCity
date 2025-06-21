#pragma once

enum PlaneType
{
    LEFT,
    RIGHT,
    BOTTOM,
    TOP,
    FRONT,
    BACK,
    PlaneType_Count
};

struct Frustum
{
    glm::vec4 planes[PlaneType_Count];
};

internal void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4& matrix);
