namespace city
{

typedef S64 WsId;
struct AgentModelRenderInfo
{
    // rendering
    Buffer<render::AgentModelInfo> geometry;
    Buffer<render::Handle> texture_handles;
};
struct Agent
{
    glm::dvec2 cartographic_coords;
    glm::dvec3 ecef_coord;
    glm::dvec3 ecef_dir;
    U64 latest_update_frame;
    VehicleType vehicle_type;
    F32 height;
    B32 height_stop;

    // rendering
    render::Transform model_matrix;
};
struct AgentMapItem
{
    Agent* agent;
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

struct AgentSim
{
    Allocator* allocator;

    String8 asset_dir;
    String8 texture_dir;
    U32 agent_count;
    U32 max_agent_count;

    AgentConfig agent_config[(S32)VehicleType::Count];
    AgentModelRenderInfo models[(S32)VehicleType::Count];
    Map<WsId, AgentMapItem>* agent_map;
    ArenaArray<Agent>* agents_active; // IMPORTANT: The implementation does not allow agents to be removed at the moment. E.g. Agent pointer are in flight using cesium height sampling function
};

g_internal void
agents_create(AgentSim* car_sim);
g_internal void
agent_sim_destroy(AgentSim* car_sim);
g_internal void
agent_sim_update(AgentSim* agent_sim, cesium::TilesetRenderer* renderer, Buffer<Coordinate> coord_buffer, glm::dmat4& ecef_to_local, F32 scale_factor, U64 cur_frame);
g_internal void
agent_draw(render::MappedHandle<void> camera_handle, Buffer<render::AgentModelInfo> meshes, Buffer<render::Handle> texture_handles, render::BufferInfo* instance_buffer_info);
g_internal glm::dmat3
_gltf_rotation_to_world(glm::dvec3 world_dir, glm::dvec3 model_dir);
g_internal void
_agent_height_update_async(cesium::TilesetRenderer* renderer, Agent* agent);
} // namespace city
