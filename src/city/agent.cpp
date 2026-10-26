#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/cache.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "utility/utility_inc.hpp"
#include "async/async_inc.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/misc_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "gltfw/gltfw.hpp"
#include "osm/osm.hpp"
#include "cesium/cesium_tileset.hpp"
#include "city/city_inc.hpp"
#include "entrypoint.hpp"
#include "city/agent.hpp"
#include "city/city.hpp"
#include "city/tile_bvh.hpp"
#include "render/tile_bvh.hpp"
#include "base/base_allocator_templates.hpp"
#include "base/base_allocator.hpp"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include "base/base_templates.hpp"
#include "base/base_strings.hpp"
#include <CesiumGeospatial/Cartographic.h>
#include <CesiumGeospatial/Ellipsoid.h>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_double4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace city
{

AgentModelRenderInfo
agent_model_render_info_create(render::ThreadWorkerCmdCtx* t_ctx, gltfw_Result& glb_result, Vec3F32& model_pivot)
{
    ScratchScope scratch = ScratchScope(0, 0);
    AgentModelRenderInfo info = {};

    info.geometry = Array<render::AgentModelInfo>(glb_result.primitives.count);
    info.texture_handles = Array<render::Handle>(glb_result.textures.size);
    Assert(info.texture_handles.size);
    Assert(info.geometry.size);

    // assign texture handles
    {
        info.texture_handles[0] = render::texture_zero_handle_get();
        for (U32 tex_idx = 1; tex_idx < glb_result.textures.size; ++tex_idx)
        {
            gltfw_Texture* tex = &glb_result.textures[tex_idx];
            render::SamplerInfo sampler_info = sampler_from_cgltf_sampler(tex->sampler);
            info.texture_handles.data[tex_idx] =
                render::texture_load_sync(t_ctx, &sampler_info, tex->tex_buf);
        }
    }

    U32 mesh_idx = 0;
    for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
    {
        Assert(node->tex_idx < info.texture_handles.size);

        // vertex and index extraction
        Buffer<render::PrimitiveVertex> vertex_buffer =
            buffer_alloc<render::PrimitiveVertex>(scratch.arena, node->vertices.size);
        for (U64 vertex_idx = 0; vertex_idx < node->vertices.size; ++vertex_idx)
        {
            gltfw_Vertex3D& source = node->vertices.data[vertex_idx];
            vertex_buffer.data[vertex_idx] = {.pos = glm::vec3(source.pos.x, source.pos.y, source.pos.z),
                                              .normal = glm::vec3(0, 1, 0),
                                              .color = node->color,
                                              .uv = glm::vec2(source.uv.x, source.uv.y)};
        }

        // offset vertices based on new pivot
        for (U32 vertex_idx = 0; vertex_idx < vertex_buffer.size; vertex_idx++)
        {
            vertex_buffer.data[vertex_idx].pos.x -= model_pivot.x;
            vertex_buffer.data[vertex_idx].pos.y -= model_pivot.y;
            vertex_buffer.data[vertex_idx].pos.z -= model_pivot.z;
        }

        // load geometry and textures

        info.geometry.data[mesh_idx].texture_h = info.texture_handles[node->tex_idx];
        info.geometry.data[mesh_idx].color = node->color;
        info.geometry.data[mesh_idx].lod_geometry = render::render_model_lod_upload(vertex_buffer, node->indices, scratch.arena);

        mesh_idx++;
    }
    return info;
}

// IMPORTANT: Runs on worker thread
g_internal void
agents_create(AgentSimThread* agent_sim_thread)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    AgentSimConfig& agent_sim_config = agent_sim_thread->config;

    render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread_ctx);
    defer(render::thread_cmd_buffer_end(thread_ctx));

    for (U32 agent_cfg_idx = 0; agent_cfg_idx < ArrayCount(agent_sim_config.agent_config); ++agent_cfg_idx)
    {
        AgentConfig* agent_config = &agent_sim_config.agent_config[agent_cfg_idx];
        String8 glb_path =
            str8_path_from_str8_list(scratch.arena, {agent_sim_config.asset_dir, agent_sim_config.agent_config[agent_cfg_idx].asset_file_name});
        gltfw_Result glb_result = gltfw_glb_read(scratch.arena, glb_path);

        // calculate model bounds
        Rng3F32 model_bounds = {};
        Vec3F32 model_pivot = {};
        {
            B32 model_bounds_initialized = false;
            for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
            {
                Rng3F32 primitive_bounds = gltfw_model_bounds_calc(node->vertices);
                if (!model_bounds_initialized)
                {
                    model_bounds = primitive_bounds;
                    model_bounds_initialized = true;
                }
                else
                {
                    model_bounds.min.x = Min(model_bounds.min.x, primitive_bounds.min.x);
                    model_bounds.min.y = Min(model_bounds.min.y, primitive_bounds.min.y);
                    model_bounds.min.z = Min(model_bounds.min.z, primitive_bounds.min.z);

                    model_bounds.max.x = Max(model_bounds.max.x, primitive_bounds.max.x);
                    model_bounds.max.y = Max(model_bounds.max.y, primitive_bounds.max.y);
                    model_bounds.max.z = Max(model_bounds.max.z, primitive_bounds.max.z);
                }
            }

            // set pivot based on model bounds
            model_pivot.x = (model_bounds.min.x + model_bounds.max.x) * 0.5f;
            model_pivot.y = model_bounds.min.y;
            model_pivot.z = (model_bounds.min.z + model_bounds.max.z) * 0.5f;
        }
        agent_config->model_bounds = sub_rng3f32(model_bounds, model_pivot);

        agent_sim_config.models[agent_cfg_idx] = agent_model_render_info_create(thread_ctx, glb_result, model_pivot);
    }
}

AgentSimThread::~AgentSimThread()
{
    // Moved model arrays are empty after successful publication.
    _agent_models_release(&config);
}

AgentSystem::~AgentSystem()
{
    destroy();
}

void
AgentSystem::destroy()
{
    if (this->height_sampler)
    {
        this->height_sampler->destroy();
        this->height_sampler = {};
    }
    this->height_requests = {};
    if (agent_sim_config)
    {
        AgentSimConfig* config = agent_sim_config.get();
        _agent_models_release(config);
    }
    agent_sim.reset();
    agent_sim_config.reset();
}

void
AgentSystem::update(ui::Camera* camera,
                    render::MeshHandle& hover_icon_mesh_handle, cesium::TilesetRenderer* tileset,
                    Buffer<AgentUpdate>& agent_updates, bool agent_reset, bool snapshot_received,
                    F32 agent_scale_factor, glm::dmat4& ecef_to_local, F64 playback_time)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    if (agent_reset)
    {
        this->agent_sim->agents_active.invalidate_all();
        this->agent_sim->agent_map.clear();
    }
    if (snapshot_received)
    {
        this->agent_sim->reconcile_snapshot(agent_updates);
    }
    bool agents_created = false;
    for (U32 agent_idx = 0; agent_idx < agent_updates.size; agent_idx++)
    {
        AgentSlotUpdate slot = this->agent_sim->update_slot(agent_updates[agent_idx]);
        agents_created |= slot.created;
    }
    if (agents_created)
        this->_agent_height_updates_start(tileset);

    // Evaluate from the event's original position each frame, including frames
    // without a new stream reply. Playback time also handles pause and speed changes.
    this->agent_sim->update_positions(playback_time);
    for (Agent& agent : this->agent_sim->agents_active)
    {
        if (!agent.transform_dirty)
            continue;
        auto cartographic = CesiumGeospatial::Ellipsoid::WGS84.cartesianToCartographic(agent.ecef_coord_cur);
        Assert(cartographic.has_value());
        agent.cartographic_coords = glm::dvec2(glm::degrees(cartographic->longitude),
                                                glm::degrees(cartographic->latitude));
    }

    this->_agent_height_update_async(tileset);
    // Height results can arrive on frames without simulator movement updates.
    // Recalculate positions and bounds only after movement or a changed height.
    for (Agent& agent : this->agent_sim->agents_active)
    {
        if (!agent.transform_dirty)
            continue;
        AgentConfig& config = this->agent_sim_config->agent_config[enum_idx(agent.vehicle_type)];
        glm::dvec3 local_world_dir = glm::dvec3(ecef_to_local * glm::dvec4(agent.ecef_dir, 0.0));
        glm::dmat3 model_to_world_rotation =
            _gltf_rotation_to_world(local_world_dir, glm::dvec3(config.model_forward_dir));
        F32 model_scale = agent_scale_factor + config.model_to_world_scale;
        agent.model_matrix.x_basis = glm::vec4(glm::dvec4(model_to_world_rotation[0], 0.0)) * model_scale;
        agent.model_matrix.y_basis = glm::vec4(glm::dvec4(model_to_world_rotation[1], 0.0)) * model_scale;
        agent.model_matrix.z_basis = glm::vec4(glm::dvec4(model_to_world_rotation[2], 0.0)) * model_scale;
        CesiumGeospatial::Cartographic cartographic_from(glm::radians(agent.cartographic_coords.x),
                                                         glm::radians(agent.cartographic_coords.y), agent.height);
        glm::dvec3 elevated_ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(cartographic_from);
        agent.model_matrix.w_basis = glm::vec4(ecef_to_local * glm::dvec4(elevated_ecef, 1.0));
        glm::mat4 transform(agent.model_matrix.x_basis, agent.model_matrix.y_basis,
                            agent.model_matrix.z_basis, agent.model_matrix.w_basis);
        agent.world_bounds = _agent_world_bounds_from_transform(config.model_bounds, transform);
        agent.transform_dirty = false;
    }

    // rendering
    {
        AgentModelRenderInfo* models = this->agent_sim_config->models;

        ChunkList<render::Transform>* transform_lists[ArrayCount(this->agent_sim_config->agent_config)];
        for (U32 agent_cfg_idx = 0; agent_cfg_idx < ArrayCount(transform_lists); ++agent_cfg_idx)
        {
            transform_lists[agent_cfg_idx] = chunk_list_create<render::Transform>(scratch.arena, 100);
        }
        for (Agent& active_agent : this->agent_sim->agents_active)
        {
            B32 visible = ui::frustum_check_from_bounding_box(&camera->frustum_planes, active_agent.world_bounds);
            this->agent_icon_add(active_agent, hover_icon_mesh_handle,
                                 hover_icon_scale_factor); // TODO: Frustum cull icon as well
            if (visible)
            {
                if (ui::is_bounding_sphere_to_be_culled(*camera, active_agent.world_bounds) == false)
                {
                    ChunkList<render::Transform>* transform_list = transform_lists[enum_idx(active_agent.vehicle_type)];
                    chunk_list_insert(scratch.arena, transform_list, active_agent.model_matrix);
                }
            }
        }

        // Submit each model once after gathering all visible agent transforms.
        for (U32 agent_cfg_idx = 0; agent_cfg_idx < ArrayCount(this->agent_sim_config->agent_config); ++agent_cfg_idx)
        {
            AgentModelRenderInfo* model_render_info = &models[agent_cfg_idx];

            Buffer<render::Transform> transform_buffer =
                buffer_from_chunk_list(draw::draw_frame_arena_get(), transform_lists[agent_cfg_idx]);
            render::BufferInfo instance_buffer_info =
                render::BufferInfo(transform_buffer, render::BufferType_Vertex | render::BufferType_StorageBuffer);

            this->agent_draw(model_render_info->geometry, &instance_buffer_info);
        }
    }
}

void
AgentSystem::agent_icon_add(Agent& agent, render::MeshHandle hover_icon_mesh_handle, F32 hover_icon_scale_factor)
{
    glm::vec3 agent_location = glm::vec3(agent.model_matrix.w_basis);
    F32 agent_height = agent.world_bounds.max.z - agent.world_bounds.min.z;
    glm::vec3 hover_icon_connector_location = agent_location;
    hover_icon_connector_location.z += agent_height;
    glm::vec3 hover_icon_location = hover_icon_connector_location;
    hover_icon_location.z += agent_hover_icon_offset;
    render::Line hover_icon_connector = {
        .from = hover_icon_connector_location,
        .to = hover_icon_location,
        .color = glm::vec3(agent_hover_icon_color),
    };
    draw::draw_line(hover_icon_connector);
    draw::primitive_draw(hover_icon_location, hover_icon_scale_factor, hover_icon_mesh_handle);
}

void
AgentSystem::agent_draw(Array<render::AgentModelInfo>& meshes, render::BufferInfo* instance_buffer_info)
{
    draw::DrawFrame* frame = draw::draw_frame_get();
    Assert(instance_buffer_info->type_size == sizeof(render::Transform));
    Buffer<render::Transform> transforms = {(render::Transform*)instance_buffer_info->buffer.data,
                                            instance_buffer_info->elem_count};
    Arena* frame_arena = draw::draw_frame_arena_get();
    Buffer<render::Transform> frame_transforms = buffer_arena_copy(frame_arena, transforms);
    for (render::AgentModelInfo& mesh : meshes)
    {
        render::AgentMeshInstanceBatch batch = {};
        batch.agent_model_info = mesh;
        batch.transforms = frame_transforms;
        batch.texture_handle = mesh.texture_h;
        batch.textured = true;
        chunk_list_insert(frame_arena, frame->mesh_instance_batches, batch);
    }
}

void
AgentSystem::_agent_height_updates_start(cesium::TilesetRenderer* renderer)
{
    (void)renderer;
    this->agent_sim->height_updates_stop = false;
}

void
AgentSystem::_agent_height_updates_stop()
{
    if (!this->agent_sim->height_updates_stop)
    {
        ++this->height_epoch;
        for (Agent& agent : this->agent_sim->agents_active)
            agent.height_request_id = 0;
    }
    this->agent_sim->height_updates_stop = true;
}

void
AgentSystem::_agent_height_update_async(cesium::TilesetRenderer* renderer)
{
    if (this->agent_sim->height_updates_stop || renderer->destruction_requested)
        return;
    Allocator* allocator = &this->agent_sim->allocator;
    Arena* arena = allocator->arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    U32 capacity = this->agent_sim_config->max_agent_count;
    if (!this->height_sampler)
    {
        if (this->agent_sim->agents_active.item_in_use_count == 0)
            return;
        this->height_sampler = allocator->make<TileHeightSampler>(capacity);
        this->height_requests = Array<AgentHeightRequest>(allocator, capacity + 1);
    }

    // Results identify the original agent generation and streaming epoch. A single
    // outstanding request per agent prevents old batches from overwriting newer samples.
    Buffer<TileHeightResult> results = buffer_alloc<TileHeightResult>(scratch.arena, capacity);
    U32 result_count = this->height_sampler->collect(results);
    // The published BVH and completed batches may precede the current draw list.
    // Build a direct handle lookup once so hidden/reused entries cannot apply a
    // stale height while a replacement hierarchy is being uploaded.
    U32 maximum_drawn_entry = 0;
    if (result_count)
        for (cesium::TileRenderResources* tile = renderer->tile_to_show_first; tile; tile = tile->render_next)
            for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
                maximum_drawn_entry = Max(maximum_drawn_entry, batch->mesh_bvh_entry.handle.idx);
    Buffer<U32> drawn_generations = buffer_alloc<U32>(scratch.arena, (U64)maximum_drawn_entry + 1);
    if (result_count)
        for (cesium::TileRenderResources* tile = renderer->tile_to_show_first; tile; tile = tile->render_next)
            for (cesium::TileDrawBatch* batch = tile->batch_first; batch; batch = batch->next)
                drawn_generations.data[batch->mesh_bvh_entry.handle.idx] = batch->mesh_bvh_entry.handle.gen_id;
    for (U32 index = 0; index < result_count; ++index)
    {
        TileHeightResult result = results.data[index];
        B32 entry_drawn = result.entry_handle.idx != 0 && result.entry_handle.idx < drawn_generations.size &&
                          drawn_generations.data[result.entry_handle.idx] == result.entry_handle.gen_id;
        if (!entry_drawn)
            result.hit = false;
        const AgentHeightRequest& request = this->height_requests.data[result.request_id % this->height_requests.size];
        if (request.request_id != result.request_id || request.epoch != this->height_epoch)
            continue;
        Agent* agent = {};
        if (this->agent_sim->agents_active.item_from_handle(request.agent_handle, &agent))
            agent_height_result_apply(agent, request, result, this->height_epoch);
    }

    if (this->agent_sim->agents_active.item_in_use_count == 0)
        return;
    render::TileBvhResourceView view = {};
    if (!render::tile_bvh_resource_get(renderer->ready_tile_bvh, &view) || view.entry_count == 0)
        return;
    Buffer<TileHeightQuery> queries = buffer_alloc<TileHeightQuery>(scratch.arena, capacity);
    Buffer<AgentHeightRequest> requests = buffer_alloc<AgentHeightRequest>(scratch.arena, capacity);
    queries.size = 0;
    for (Agent& agent : this->agent_sim->agents_active)
    {
        if (agent.done || agent.height_request_id)
            continue;
        CesiumGeospatial::Cartographic cartographic(glm::radians(agent.cartographic_coords.x),
                                                    glm::radians(agent.cartographic_coords.y), 0.0);
        glm::dvec3 ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(cartographic);
        glm::dvec3 ecef_up = CesiumGeospatial::Ellipsoid::WGS84.geodeticSurfaceNormal(cartographic);
        glm::dvec3 surface(renderer->ecef_to_local * glm::dvec4(ecef, 1.0));
        glm::dvec3 up = glm::normalize(glm::dvec3(renderer->ecef_to_local * glm::dvec4(ecef_up, 0.0)));
        TileHeightRay ray = tile_height_ray_create(surface, up, view.city_bounds);
        // Encode the stable agent slot in the remainder. One pending sample per
        // agent keeps metadata bounded without overwriting a different agent's request.
        Assert(this->height_request_sequence < max_U64 / this->height_requests.size - 1);
        ray.query.request_id = ++this->height_request_sequence * this->height_requests.size + agent.handle.idx;
        Assert(ray.query.request_id != 0);
        requests.data[queries.size] = {ray.query.request_id, this->height_epoch, agent.handle,
                                      agent.ecef_coord_cur, ray.origin_height};
        queries.data[queries.size++] = ray.query;
    }
    if (queries.size && this->height_sampler->submit(renderer->ready_tile_bvh, {queries.data, queries.size}))
        for (U32 index = 0; index < queries.size; ++index)
        {
            const AgentHeightRequest& request = requests.data[index];
            this->height_requests.data[request.request_id % this->height_requests.size] = request;
            Agent* agent = {};
            B32 active = this->agent_sim->agents_active.item_from_handle(request.agent_handle, &agent);
            Assert(active);
            agent->height_request_id = request.request_id;
        }
}

// assumes forward direction is in the x direction and model coordinate system having +Y as upd and +Z being up in world
// coordinate system.
g_internal glm::dmat3
_gltf_rotation_to_world(glm::dvec3 world_dir, glm::dvec3 model_dir)
{
    using glm::dmat3;
    using glm::dvec3;

    // build model basis vectors/matrix
    dvec3 model_up = glm::dvec3(0.0, 1.0, 0.0);
    dvec3 model_basis_forward = glm::normalize(model_dir);
    dvec3 model_basis_side = glm::normalize(glm::cross(model_up, model_basis_forward));
    dvec3 model_basis_up = glm::normalize(glm::cross(model_basis_forward, model_basis_side));
    dmat3 model_basis = dmat3(model_basis_forward, model_basis_side, model_basis_up);

    // build world basis
    dvec3 world_up = glm::dvec3(0.0, 0.0, 1.0);
    dvec3 world_basis_forward = glm::normalize(world_dir);
    dvec3 world_basis_side = glm::normalize(glm::cross(world_up, world_basis_forward));
    dvec3 world_basis_up = glm::normalize(glm::cross(world_basis_forward, world_basis_side));
    dmat3 world_basis = dmat3(world_basis_forward, world_basis_side, world_basis_up);

    // model to work rotation R * model_basis = world_basise <==> R = world_basis * model_basis_inv (inv = transpose
    // because of orthonormality)
    dmat3 model_to_world_rotation = world_basis * glm::transpose(model_basis);

    return model_to_world_rotation;
}
g_internal void
_agent_models_release(AgentSimConfig* config)
{
    for (AgentModelRenderInfo& model : config->models)
    {
        for (auto& geometry : model.geometry)
        {
            render::handle_destroy(geometry.lod_geometry.lod_buffer_handle);
            render::handle_destroy(geometry.lod_geometry.vertex_buffer_handle);
            geometry = {};
        }
        // Index zero is the renderer's shared fallback texture.
        for (U32 i = 1; i < model.texture_handles.size; ++i)
        {
            render::handle_destroy(model.texture_handles[i]);
            model.texture_handles[i] = {};
        }
    }
}

} // namespace city
