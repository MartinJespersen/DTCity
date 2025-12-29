namespace ui
{
struct Camera
{
    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
    glm::vec3 position;
    glm::vec3 view_dir;
    Vec2F64 mouse_pos_last;
    F32 zoom_sensitivity;
    F32 fov;
    F32 yaw;
    F32 pitch;
    Vec2F64 world_offset;
};

static void
camera_init(Camera* camera, Vec2F32 start_pos);
static void
camera_update(Camera* camera, io_IO* input, F32 time, Vec2U32 extent);

} // namespace ui
