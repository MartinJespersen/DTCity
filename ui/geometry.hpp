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

struct UI_Camera
{
    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
    F32 zoom;
};

internal void
UI_CameraInit(UI_Camera* camera);

internal void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix);

internal void
UI_CameraUpdate(UI_Camera* camera, IO* input, VkExtent2D extent);
