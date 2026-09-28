namespace city
{

// IMPORTANT: Runs on worker thread
g_internal void
agents_create(AgentSimThread* agent_sim_thread)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    agent_sim_thread->agent_sim = std::make_unique<AgentSim>(Allocator::create<AgentSim>());
    AgentSimConfig& agent_sim_config = agent_sim_thread->config;
    AgentSim& agent_sim = *agent_sim_thread->agent_sim;

    for (U32 agent_cfg_idx = 0; agent_cfg_idx < ArrayCount(agent_sim_config.agent_config); ++agent_cfg_idx)
    {
        AgentConfig* agent_config = &agent_sim_config.agent_config[agent_cfg_idx];
        AgentModelRenderInfo* model_render_info = &agent_sim_config.models[agent_cfg_idx];

        // parse glb file
        String8 glb_path =
            str8_path_from_str8_list(scratch.arena, {agent_sim_config.asset_dir, agent_config->asset_file_name});
        gltfw_Result glb_result = gltfw_glb_read(scratch.arena, glb_path);
        U32 primitive_count = 0;
        for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
        {
            primitive_count++;
        }
        AssertAlways(primitive_count > 0);

        // calculate model bounds
        Rng3F32 model_bounds = {};
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
        Vec3F32 model_pivot = {};
        model_pivot.x = (model_bounds.min.x + model_bounds.max.x) * 0.5f;
        model_pivot.y = model_bounds.min.y;
        model_pivot.z = (model_bounds.min.z + model_bounds.max.z) * 0.5f;

        // new bounds from pivot
        agent_config->model_bounds = sub_rng3f32(model_bounds, model_pivot);

        model_render_info->geometry = Array<render::AgentModelInfo>(primitive_count);
        model_render_info->texture_handles = Array<render::Handle>(glb_result.textures.size);
        for (auto& geometry : model_render_info->geometry)
            geometry = {};
        for (auto& texture : model_render_info->texture_handles)
            texture = {};
        render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
        render::thread_cmd_buffer_record(thread_ctx);
        defer(render::thread_cmd_buffer_end(thread_ctx));

        Assert(model_render_info->texture_handles.size > 0);
        model_render_info->texture_handles[0] = render::texture_zero_handle_get();
        for (U32 tex_idx = 1; tex_idx < glb_result.textures.size; ++tex_idx)
        {
            gltfw_Texture* tex = glb_result.textures[tex_idx];
            render::SamplerInfo sampler_info = sampler_from_cgltf_sampler(tex->sampler);
            model_render_info->texture_handles.data[tex_idx] =
                render::texture_load_sync(thread_ctx, &sampler_info, tex->tex_buf);
        }

        U32 mesh_idx = 0;
        for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
        {
            Assert(node->tex_idx < model_render_info->texture_handles.size);

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
            render::MeshletMeshHandle mesh =
                render::mesh_shader_handles_create_and_upload(vertex_buffer, node->indices, scratch.arena);
            model_render_info->geometry.data[mesh_idx].vertex_handle = mesh.vertex_buffer_handle;
            model_render_info->geometry.data[mesh_idx].index_handle = mesh.meshlet_buffer_handle;
            model_render_info->geometry.data[mesh_idx].meshlet_count = mesh.meshlet_count;
            model_render_info->geometry.data[mesh_idx].texture_handle_idx = node->tex_idx;
            model_render_info->geometry.data[mesh_idx].color = node->color;

            mesh_idx++;
        }
    }

    agent_sim.agent_map = agent_sim.allocator.make<Map<WsId, AgentMapItem>>(agent_sim_config.agent_count);

    agent_sim.agents_active = agent_sim.allocator.make<ArrayResourcePool<Agent>>(agent_sim_config.max_agent_count);
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
    Assert(!agent_sim || !agent_sim->height_update_in_flight);
    if (agent_sim_config)
    {
        AgentSimConfig* config = agent_sim_config.get();
        _agent_models_release(config);
    }
    agent_sim.reset();
    agent_sim_config.reset();
}

void
AgentSystem::update(cesium::TilesetRenderer* renderer, CoordinateBatch updates, glm::dmat4& ecef_to_local,
                    F32 scale_factor, U64 cur_frame)
{
    prof_scope_marker;

    if (updates.reset)
    {
        this->agent_sim->agents_active->invalidate_all();
        this->agent_sim->agent_map->clear();
    }
    Buffer<Coordinate> coord_buffer = updates.coordinates;
    for (U32 agent_idx = 0; agent_idx < coord_buffer.size; agent_idx++)
    {
        Coordinate* coord = &coord_buffer.data[agent_idx];

        glm::dvec3 ecef_coord = util::ecef_from_wgs84(coord->from.x, coord->from.y);
        Agent* agent = {};
        AgentMapItem* map_item = {};
        MapResult result = this->agent_sim->agent_map->get(coord->id, &map_item);
        if (result == MapResult::Success)
        {
            bool agent_found = agent_sim->agents_active->item_from_handle(map_item->agent_handle, &agent);
            Assert(agent_found);
            if (agent_found)
            {
                glm::dvec3 ecef_dir = ecef_coord - agent->ecef_coord;
                F64 move_len_sq = glm::dot(ecef_dir, ecef_dir);
                if (move_len_sq > 0.001)
                {
                    agent->ecef_coord = ecef_coord;
                    agent->ecef_dir = ecef_dir;
                }
            }
        }
        else
        {
            glm::dvec3 local_dir = glm::dvec3(1, 0, 0);
            ArrayResourcePoolHandle handle = agent_sim->agents_active->item_new(&agent);
            *agent = {
                .handle = handle, .ecef_coord = ecef_coord, .ecef_dir = local_dir, .vehicle_type = coord->vehicle_type};
            agent->vehicle_type = coord->vehicle_type;
            AgentMapItem agent_map_item = {.agent_handle = handle};
            map_item = agent_sim->agent_map->insert(agent_sim->allocator.arena, coord->id, agent_map_item);

            agent->cartographic_coords = glm::dvec2(coord->from.x, coord->from.y);
            _agent_height_updates_start(renderer);
        }
        agent->cartographic_coords = glm::dvec2(coord->from.x, coord->from.y);

        AgentConfig* agent_config = &agent_sim_config->agent_config[enum_idx(agent->vehicle_type)];

        glm::dvec3 local_world_dir = glm::dvec3(ecef_to_local * glm::dvec4(agent->ecef_dir, 0.0));
        glm::dmat3 model_to_world_rotation =
            _gltf_rotation_to_world(local_world_dir, glm::dvec3(agent_config->model_forward_dir));

        agent->model_matrix.x_basis = glm::vec4(glm::dvec4(model_to_world_rotation[0], 0.0f)) *
                                      (scale_factor + agent_config->model_to_world_scale);
        agent->model_matrix.y_basis = glm::vec4(glm::dvec4(model_to_world_rotation[1], 0.0f)) *
                                      (scale_factor + agent_config->model_to_world_scale);
        agent->model_matrix.z_basis = glm::vec4(glm::dvec4(model_to_world_rotation[2], 0.0f)) *
                                      (scale_factor + agent_config->model_to_world_scale);
        CesiumGeospatial::Cartographic elevated_cartographic(glm::radians(coord->from.x), glm::radians(coord->from.y),
                                                             agent->height);
        glm::dvec3 elevated_ecef_coord =
            CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(elevated_cartographic);
        agent->model_matrix.w_basis = glm::vec4(ecef_to_local * glm::dvec4(elevated_ecef_coord, 1.0));

        glm::mat4 model_transform = glm::mat4(agent->model_matrix.x_basis, agent->model_matrix.y_basis,
                                              agent->model_matrix.z_basis, agent->model_matrix.w_basis);
        agent->world_bounds = _agent_world_bounds_from_transform(agent_config->model_bounds, model_transform);
        agent->latest_update_frame = cur_frame;
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
AgentSystem::agent_draw(render::MappedHandle<void> camera_handle, Array<render::AgentModelInfo>& meshes,
                        Array<render::Handle>& texture_handles, render::BufferInfo* instance_buffer_info)
{
    draw::DrawFrame* frame = draw::draw_frame_get();
    (void)camera_handle; // The draw layer owns the frame camera.
    Assert(instance_buffer_info->type_size == sizeof(render::Transform));
    Buffer<render::Transform> transforms = {(render::Transform*)instance_buffer_info->buffer.data,
                                            instance_buffer_info->elem_count};
    Arena* frame_arena = draw::draw_frame_arena_get();
    Buffer<render::Transform> frame_transforms = buffer_arena_copy(frame_arena, transforms);
    for (render::AgentModelInfo& mesh : meshes)
    {
        render::MeshInstanceBatch batch = {};
        batch.transforms = frame_transforms;
        batch.mesh_handle = {mesh.vertex_handle, mesh.index_handle, mesh.meshlet_count};
        batch.texture_handle = texture_handles.data[mesh.texture_handle_idx];
        batch.textured = true;
        batch.lod_error_pixels = 1.0f;
        chunk_list_insert(frame_arena, frame->mesh_instance_batches, batch);
    }
}

void
AgentSystem::_agent_height_updates_start(cesium::TilesetRenderer* renderer)
{
    if (agent_sim->agents_active == 0 || agent_sim->agents_active->item_in_use_count == 0 ||
        agent_sim->height_update_in_flight)
    {
        return;
    }

    agent_sim->height_updates_stop = false;
    _agent_height_update_async(renderer);
}

void
AgentSystem::_agent_height_updates_stop()
{
    agent_sim->height_updates_stop = true;
}

// TODO: This could be written as a general height calculation inside the cesium layer
void
AgentSystem::_agent_height_update_async(cesium::TilesetRenderer* renderer)
{
    if (agent_sim->height_updates_stop || renderer->height_sample_stop)
    {
        agent_sim->height_update_in_flight = false;
        return;
    }

    std::vector<CesiumGeospatial::Cartographic> agent_positions;
    std::vector<ArrayResourcePoolHandle> sampled_agents;
    agent_positions.reserve(agent_sim->agents_active->item_in_use_count);
    sampled_agents.reserve(agent_sim->agents_active->item_in_use_count);

    for (Agent& agent : *agent_sim->agents_active)
    {
        if (!agent.done)
        {
            CesiumGeospatial::Cartographic agent_position(glm::radians(agent.cartographic_coords.x),
                                                          glm::radians(agent.cartographic_coords.y), 0.0);
            agent_positions.emplace_back(agent_position);
            sampled_agents.emplace_back(agent.handle);
        }
    }

    if (agent_positions.empty())
    {
        agent_sim->height_update_in_flight = false;
        return;
    }

    agent_sim->height_update_in_flight = true;
    // The single tileset is also the displayed source.
    CesiumAsync::Future<Cesium3DTilesSelection::SampleHeightResult> height_future =
        renderer->tilesets.data[0]->sampleHeightMostDetailed(std::move(agent_positions));
    std::move(height_future)
        .thenInMainThread(
            [renderer, this,
             sampled_agents = std::move(sampled_agents)](Cesium3DTilesSelection::SampleHeightResult&& result)
            {
                Assert(result.positions.size() == sampled_agents.size());
                Assert(result.sampleSuccess.size() == sampled_agents.size());
                for (U64 agent_idx = 0; agent_idx < sampled_agents.size(); ++agent_idx)
                {
                    if (result.sampleSuccess[agent_idx])
                    {
                        ArrayResourcePoolHandle handle = sampled_agents[agent_idx];
                        Agent* agent = {};
                        if (this->agent_sim->agents_active->item_from_handle(handle, &agent))
                        {
                            agent->height = (F32)result.positions[agent_idx].height;
                        }
                    }
                }

                agent_sim->height_update_in_flight = false;
                _agent_height_update_async(renderer);
            })
        .catchInMainThread(
            [this](std::exception&& e)
            {
                this->agent_sim->height_update_in_flight = false;
                DEBUG_LOG("Agent height sampling failed: %s\n", e.what());
            });
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
            render::handle_destroy(geometry.vertex_handle);
            render::handle_destroy(geometry.index_handle);
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
