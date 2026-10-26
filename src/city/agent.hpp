#pragma once

#include "render/render.hpp"
#include "simulator_messages.hpp"
#include "gltfw/gltfw.hpp"
#include "city/tile_bvh.hpp"
#include "base/base_core.hpp"
#include "base/base_container.hpp"
#include "base/container.hpp"
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
    ArrayResourcePoolHandle handle;
    WsId id;
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
    U32 agent_map_bucket_count;
    U32 max_agent_count;
    AgentConfig agent_config[(S32)AgentType::Count];
    AgentModelRenderInfo models[(S32)AgentType::Count];
};

struct AgentSlotUpdate
{
    Agent* agent;
    bool active;
    bool created;
};

struct AgentSim
{
    Allocator allocator;

    B32 height_updates_stop;
    Map<WsId, AgentMapItem> agent_map;
    ArrayResourcePool<Agent> agents_active;

    AgentSim(U32 hashmap_bucket_capacity, U32 max_agent_count);
    AgentSlotUpdate
    update_slot(const AgentUpdate& update);
    void
    update_positions(F64 playback_time);
    void
    reconcile_snapshot(Buffer<AgentUpdate> updates);
};

struct AgentSimThread
{
    AgentSimConfig config; // input
    ~AgentSimThread();
};

struct AgentHeightRequest
{
    U64 request_id;
    U64 epoch;
    ArrayResourcePoolHandle agent_handle;
    glm::dvec3 sampled_ecef;
    F64 origin_height;
};

// Applies only a matching generation/request/epoch. Completed misses and samples
// more than five metres behind the agent clear the request and keep its old height.
B32
agent_height_result_apply(Agent* agent, const AgentHeightRequest& request,
                          const TileHeightResult& result, U64 epoch);

g_internal glm::dmat3
_gltf_rotation_to_world(glm::dvec3 world_dir, glm::dvec3 model_dir);

struct AgentSystem
{
    std::unique_ptr<AgentSimConfig> agent_sim_config; // lifetime = city lifetime
    std::unique_ptr<AgentSim> agent_sim;              // agent sim data should be able to reset it therefore has its own allocator
    TileHeightSampler* height_sampler = {};
    Array<AgentHeightRequest> height_requests = {};
    U64 height_request_sequence = {};
    U64 height_epoch = {};
    ~AgentSystem();

    void
    destroy();
    void
    update(ui::Camera* camera,
           render::MeshHandle& hover_icon_mesh_handle, cesium::TilesetRenderer* tileset,
           Buffer<AgentUpdate>& agent_updates, bool agent_reset, bool snapshot_received, F32 agent_scale_factor, glm::dmat4& ecef_to_local, F64 playback_time);

    void
    agent_icon_add(Agent& agent, render::MeshHandle hover_icon_mesh_handle, F32 hover_icon_scale_factor);
    void
    agent_draw(Array<render::AgentModelInfo>& meshes, render::BufferInfo* instance_buffer_info);
    void
    _agent_height_updates_start(cesium::TilesetRenderer* renderer);
    void
    _agent_height_updates_stop();
    void
    _agent_height_update_async(cesium::TilesetRenderer* renderer);
};

AgentModelRenderInfo
agent_model_render_info_create(render::ThreadWorkerCmdCtx* t_ctx, gltfw_Result& glb_result, Vec3F32& model_pivot);
g_internal void
agents_create(AgentSimThread* agent_sim);
g_internal void
_agent_models_release(AgentSimConfig* config);
} // namespace city
