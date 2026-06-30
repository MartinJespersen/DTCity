namespace ui
{

enum PlaneType
{
    PlaneType_Left,
    PlaneType_Right,
    PlaneType_Top,
    PlaneType_Btm,
    PlaneType_Back,
    PlaneType_Front,
    PlaneType_Count
};

struct Frustum
{
    glm::vec4 planes[PlaneType_Count];
};

struct CameraUniformBuffer
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
    glm::vec2 viewport_dim;
    glm::vec2 _padding;
};

struct Camera
{
    render::MappedHandle<CameraUniformBuffer> mut_handles;
    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
    glm::vec3 position;
    glm::vec3 view_dir;
    Vec2F64 mouse_pos_last;
    F32 move_sensitivity;
    F32 fov;
    F32 yaw;
    F32 pitch;
};

g_internal void
camera_init(Arena* arena, Camera* camera);
static void
camera_update(Camera* camera, io::IO* input, F64 time, Vec2S32 extent, bool enable);

g_internal void
_camera_uniform_buffer_update(ui::Camera* camera, render::MappedHandle<CameraUniformBuffer> mut_handle, Vec2U32 screen_res);

g_internal void
_frustum_planes_calculate(Frustum* out_frustum, const glm::mat4 matrix);
} // namespace ui
