namespace city
{
g_internal void
agents_create(AgentSim* agent_sim)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    for (U32 agent_cfg_idx = 0; agent_cfg_idx < ArrayCount(agent_sim->agent_config); ++agent_cfg_idx)
    {
        AgentConfig* agent_config = &agent_sim->agent_config[agent_cfg_idx];
        AgentModelRenderInfo* model_render_info = &agent_sim->models[agent_cfg_idx];

        // parse glb file
        String8 glb_path = str8_path_from_str8_list(scratch.arena, {agent_sim->asset_dir, agent_config->asset_file_name});
        gltfw_Result glb_result = gltfw_glb_read(agent_sim->allocator->arena, glb_path);
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

        model_render_info->geometry = buffer_alloc<render::AgentModelInfo>(agent_sim->allocator->arena, primitive_count);
        model_render_info->texture_handles = buffer_alloc<render::Handle>(agent_sim->allocator->arena, glb_result.textures.size);
        render::ThreadWorkerCmdCtx* thread_ctx = render::thread_ctx_create();
        render::thread_cmd_buffer_record(thread_ctx);
        defer(render::thread_cmd_buffer_end(thread_ctx));

        Assert(model_render_info->texture_handles.size > 0);
        model_render_info->texture_handles.data[0] = render::texture_zero_handle_get();
        for (U32 tex_idx = 1; tex_idx < glb_result.textures.size; ++tex_idx)
        {
            gltfw_Texture* tex = glb_result.textures[tex_idx];
            render::SamplerInfo sampler_info = sampler_from_cgltf_sampler(tex->sampler);
            model_render_info->texture_handles.data[tex_idx] = render::texture_load_sync(thread_ctx, &sampler_info, tex->tex_buf);
        }

        U32 mesh_idx = 0;
        for (gltfw_Primitive* node = glb_result.primitives.first; node; node = node->next)
        {
            Assert(node->tex_idx < model_render_info->texture_handles.size);

            // vertex and index extraction
            Buffer<render::TileVertex> vertex_buffer = vertex_3d_from_gltfw_vertex(agent_sim->allocator->arena, node->vertices);

            // offset vertices based on new pivot
            for (U32 vertex_idx = 0; vertex_idx < vertex_buffer.size; vertex_idx++)
            {
                vertex_buffer.data[vertex_idx].pos.x -= model_pivot.x;
                vertex_buffer.data[vertex_idx].pos.y -= model_pivot.y;
                vertex_buffer.data[vertex_idx].pos.z -= model_pivot.z;
            }

            // load geometry and textures
            render::BufferInfo vertex_buffer_info = render::BufferInfo(vertex_buffer, render::BufferType_Vertex);
            Buffer<U32> index_buffer = buffer_arena_copy(agent_sim->allocator->arena, node->indices);
            render::BufferInfo index_buffer_info = render::BufferInfo(index_buffer, render::BufferType_Index);
            model_render_info->geometry.data[mesh_idx].vertex_handle = render::buffer_load_sync(thread_ctx, &vertex_buffer_info, S("agent_mesh_vertex"));
            model_render_info->geometry.data[mesh_idx].index_handle = render::buffer_load_sync(thread_ctx, &index_buffer_info, S("agent_mesh_index"));
            model_render_info->geometry.data[mesh_idx].texture_handle_idx = node->tex_idx;
            model_render_info->geometry.data[mesh_idx].color = node->color;

            mesh_idx++;
        }
    }

    agent_sim->agent_map = map_create<WsId, AgentMapItem>(agent_sim->allocator->arena, agent_sim->agent_count);
    agent_sim->agents_active = agent_sim->allocator->place<ArenaArray<Agent>>(agent_sim->max_agent_count);
}

g_internal void
agent_sim_destroy(AgentSim* car_sim)
{
    if (car_sim->allocator == 0)
    {
        return;
    }

    for (U32 model_idx = 0; model_idx < ArrayCount(car_sim->models); ++model_idx)
    {
        AgentModelRenderInfo* model_render_info = &car_sim->models[model_idx];

        for (auto geometry : model_render_info->geometry)
        {
            render::handle_destroy(geometry.vertex_handle);
            render::handle_destroy(geometry.index_handle);
        }

        for (auto texture_handle : model_render_info->texture_handles)
        {
            render::handle_destroy(texture_handle);
        }
    }

    Allocator::destroy(car_sim->allocator);
}

g_internal void
agent_sim_update(AgentSim* agent_sim, Buffer<Coordinate> coord_buffer, glm::dmat4& ecef_to_local, F32 scale_factor, U64 cur_frame)
{
    prof_scope_marker;

    ArenaArray<Agent>* agents_active = agent_sim->agents_active;

    for (U32 agent_idx = 0; agent_idx < coord_buffer.size; agent_idx++)
    {
        Coordinate* coord = &coord_buffer.data[agent_idx];

        glm::dvec3 ecef_coord = util::ecef_from_wgs84(coord->lon, coord->lat);
        Agent* agent = {};
        AgentMapItem* agent_ptr = {};
        MapResult result = map_get(agent_sim->agent_map, coord->id, &agent_ptr);
        if (result == MapResult::Success)
        {
            agent = agent_ptr->agent;
            glm::dvec3 ecef_dir = ecef_coord - agent->ecef_coord;

            F64 move_len_sq = glm::dot(ecef_dir, ecef_dir);
            if (move_len_sq > 0.001)
            {
                agent->ecef_coord = ecef_coord;
                agent->ecef_dir = ecef_dir;
            }
        }
        else
        {
            glm::dvec3 local_dir = glm::dvec3(1, 0, 0);
            Agent new_agent = {.ecef_coord = ecef_coord, .ecef_dir = local_dir};
            agent = agents_active->push(new_agent);
            agent->vehicle_type = coord->vehicle_type;
            AgentMapItem agent_map_item = {.agent = agent};
            agent_ptr = map_insert(agent_sim->agent_map, coord->id, agent_map_item);
        }

        AgentConfig* agent_config = &agent_sim->agent_config[enum_idx(agent->vehicle_type)];

        glm::dvec3 local_world_dir = glm::dvec3(ecef_to_local * glm::dvec4(agent->ecef_dir, 0.0));
        glm::dmat3 model_to_world_rotation = _gltf_rotation_to_world(local_world_dir, glm::dvec3(agent_config->model_forward_dir));

        agent->model_matrix.x_basis = glm::vec4(glm::dvec4(model_to_world_rotation[0], 0.0f)) * (scale_factor + agent_config->model_to_world_scale);
        agent->model_matrix.y_basis = glm::vec4(glm::dvec4(model_to_world_rotation[1], 0.0f)) * (scale_factor + agent_config->model_to_world_scale);
        agent->model_matrix.z_basis = glm::vec4(glm::dvec4(model_to_world_rotation[2], 0.0f)) * (scale_factor + agent_config->model_to_world_scale);
        agent->model_matrix.w_basis = glm::vec4(ecef_to_local * glm::dvec4(ecef_coord, 1.0));

        agent->latest_update_frame = cur_frame;
    }
}

g_internal AgentInstanceDrawResult
agent_draw(render::MappedHandle<void> camera_handle, Buffer<render::AgentModelInfo> meshes, Buffer<render::Handle> texture_handles, render::BufferInfo* instance_buffer_info)
{
    draw::DrawFrame* frame = draw::draw_frame_get();
    U32 align = 16;
    U32 instance_buffer_offset = frame->total_instance_buffer_byte_count + (align - 1);
    instance_buffer_offset -= instance_buffer_offset % align;

    frame->total_instance_buffer_byte_count = Max(frame->total_instance_buffer_byte_count, instance_buffer_offset + instance_buffer_info->buffer.size);

    AgentInstanceDrawResult result = {};
    result.render_scheduled = render::agent_instance_render_bucket_add(camera_handle, meshes, texture_handles, instance_buffer_info, instance_buffer_offset);
    result.buffer_offset = instance_buffer_offset;

    return result;
}

// assumes forward direction is in the x direction and model coordinate system having +Y as upd and +Z being up in world coordinate system.
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

    // model to work rotation R * model_basis = world_basise <==> R = world_basis * model_basis_inv (inv = transpose because of orthonormality)
    dmat3 model_to_world_rotation = world_basis * glm::transpose(model_basis);

    return model_to_world_rotation;
}
} // namespace city
