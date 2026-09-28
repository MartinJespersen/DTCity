namespace city
{

constexpr F32 agent_hover_icon_offset = 100.0f;
constexpr glm::vec4 agent_hover_icon_color = glm::vec4(1.0f, 0.65f, 0.0f, 1.0f);
typedef S64 WsId;
struct AgentModelRenderInfo
{
    // rendering
    Array<render::AgentModelInfo> geometry;
    Array<render::Handle> texture_handles;
};
struct Agent
{
    ArrayResourcePoolHandle handle;
    glm::dvec2 cartographic_coords;
    glm::dvec3 ecef_coord;
    glm::dvec3 ecef_dir;
    U64 latest_update_frame;
    VehicleType vehicle_type;
    F32 height;
    bool done;

    F64 sim_time_start;
    F64 sim_time_end;
    glm::dvec3 start_pos;
    glm::dvec3 end_pos;

    // updated per frame
    Rng3F32 world_bounds;

    // rendering
    render::Transform model_matrix;
};
struct AgentMapItem
{
    ArrayResourcePoolHandle agent_handle;
};

struct AgentConfig
{
    // static input
    String8 asset_file_name;
    glm::vec3 model_forward_dir; // +X is forward
    F32 model_to_world_scale;

    // computed
    Rng3F32 model_bounds;
};

struct AgentSimConfig
{
    String8 asset_dir;
    String8 texture_dir;
    U32 agent_count;
    U32 max_agent_count;
    AgentConfig agent_config[(S32)VehicleType::Count];
    AgentModelRenderInfo models[(S32)VehicleType::Count];
};

struct AgentSim
{
    Allocator allocator;

    B32 height_updates_stop;
    B32 height_update_in_flight;
    Map<WsId, AgentMapItem>* agent_map;
    ArrayResourcePool<Agent>* agents_active;
};

struct AgentSimThread
{
    AgentSimConfig config;               // input
    std::unique_ptr<AgentSim> agent_sim; // output
    ~AgentSimThread();
};

glm::dmat3
_gltf_rotation_to_world(glm::dvec3 world_dir, glm::dvec3 model_dir);

struct AgentSystem
{
    std::unique_ptr<AgentSimConfig> agent_sim_config; // lifetime = city lifetime
    std::unique_ptr<AgentSim> agent_sim; // agent sim data should be able to reset it therefore has its own allocator
    ~AgentSystem();

    void
    destroy();
    void
    update(cesium::TilesetRenderer* renderer, CoordinateBatch updates, glm::dmat4& ecef_to_local, F32 scale_factor,
           U64 cur_frame);

    void
    agent_icon_add(Agent& agent, render::MeshHandle hover_icon_mesh_handle, F32 hover_icon_scale_factor);
    void
    agent_draw(render::MappedHandle<void> camera_handle, Array<render::AgentModelInfo>& meshes,
               Array<render::Handle>& texture_handles, render::BufferInfo* instance_buffer_info);
    void
    _agent_height_updates_start(cesium::TilesetRenderer* renderer);
    void
    _agent_height_updates_stop();
    void
    _agent_height_update_async(cesium::TilesetRenderer* renderer);
};

g_internal void
agents_create(AgentSimThread* agent_sim);
g_internal void
_agent_models_release(AgentSimConfig* config);
} // namespace city
