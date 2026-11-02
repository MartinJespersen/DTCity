#pragma once

#include "render/render.hpp"
#include "simulator_messages.hpp"
#include "gltfw/gltfw.hpp"
#include "city/tile_bvh.hpp"
#include "base/base_core.hpp"
#include "base/base_container.hpp"
#include "base/container.hpp"
#include "osm/osm.hpp"
#include <memory>
#include <glm/ext/vector_double3.hpp>

namespace ui
{
struct Camera;
}
namespace cesium
{
struct TilesetRenderer;
}

namespace city
{

constexpr F32 hover_icon_scale_factor = 10.0f;
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
    PoolHandle handle;
    WsId id;
    S64 node_from_id;
    S64 node_to_id;
    S64 way_id; // OSM way ID from simulator origid, shared by segments of that way.
    glm::dvec2 cartographic_coords;
    glm::dvec3 ecef_coord_at_event;
    F64 event_time;
    glm::dvec3 ecef_velocity;
    glm::dvec3 ecef_coord_cur;
    glm::dvec3 ecef_dir;
    AgentType vehicle_type;
    F32 height;
    U64 height_request_id;
    B32 transform_dirty;
    bool done;

    // updated per frame
    Rng3F32 world_bounds;

    // rendering
    render::Transform model_matrix;
};
struct AgentMapItem
{
    PoolHandle agent_handle;
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

struct AgentRenderResources
{
    String8 asset_dir;
    String8 texture_dir;
    AgentConfig agent_config[(S32)AgentType::Count];
    AgentModelRenderInfo models[(S32)AgentType::Count];
};

struct AgentSlotUpdate
{
    Agent* agent;
    bool active;
    bool created;
};

struct AgentHeightRequest
{
    U64 request_id;
    U64 epoch;
    PoolHandle agent_handle;
    glm::dvec3 sampled_ecef;
    F64 origin_height;
};

struct AgentSim
{
    Allocator allocator;

    B32 height_updates_stop;
    Map<WsId, AgentMapItem> agent_map;
    Map<osm::WayId, U32> agent_count_by_way;
    ResourcePool<Agent> agents_active;

    TileHeightSampler* height_sampler = {};
    Array<AgentHeightRequest> height_requests = {};
    U64 height_request_sequence = {};
    U64 height_epoch = {};

    AgentSim(U32 hashmap_bucket_capacity, U32 max_agent_count);
    ~AgentSim() noexcept;
    AgentSlotUpdate
    update_slot(const AgentUpdate& update);
    void
    update_positions(F64 playback_time);
    void
    reconcile_snapshot(Buffer<AgentUpdate> updates);
    void
    reset() noexcept;

  private:
    void
    way_count_change(osm::WayId way_id, S32 delta) noexcept;
};

struct AgentModelLoadTask
{
    AgentRenderResources config; // input
    ~AgentModelLoadTask();
};

// Applies only a matching generation/request/epoch. Completed misses and samples
// more than five metres behind the agent clear the request and keep its old height.
B32
agent_height_result_apply(Agent* agent, const AgentHeightRequest& request,
                          const TileHeightResult& result, U64 epoch);

g_internal glm::dmat3
_gltf_rotation_to_world(glm::dvec3 world_dir, glm::dvec3 model_dir);

class AgentSystem
{
  public:
    ResourcePool<AgentSim> simulations;
    PoolHandle selected_simulation = {};
    std::unique_ptr<AgentRenderResources> render_resources;

    AgentSystem(Arena* arena, U32 simulation_capacity) noexcept;
    ~AgentSystem();
    PoolHandle simulation_create(U32 bucket_capacity, U32 agent_capacity) noexcept;
    bool simulation_select(PoolHandle handle) noexcept;
    void simulation_remove(PoolHandle handle) noexcept;
    void destroy();
    void
    update(AgentSim& simulation, ui::Camera* camera,
           render::MeshHandle& hover_icon_mesh_handle, cesium::TilesetRenderer* tileset,
           Buffer<AgentUpdate>& agent_updates, bool agent_reset, bool snapshot_received, F32 agent_scale_factor, glm::dmat4& ecef_to_local, F64 playback_time);

    void
    height_updates_start(AgentSim& simulation);
    void
    height_updates_stop(AgentSim& simulation);
  private:
    void
    agent_icon_add(Agent& agent, render::MeshHandle hover_icon_mesh_handle, F32 icon_scale_factor) noexcept;
    void
    agent_draw(Array<render::AgentModelInfo>& meshes, render::BufferInfo* instance_buffer_info);
    void height_update_async(AgentSim& simulation, cesium::TilesetRenderer* renderer);
};

AgentModelRenderInfo
agent_model_render_info_create(render::ThreadWorkerCmdCtx* t_ctx, gltfw_Result& glb_result, Vec3F32& model_pivot);
g_internal void
agent_models_load(AgentModelLoadTask* task);
g_internal void
_agent_models_release(AgentRenderResources* config);
} // namespace city
