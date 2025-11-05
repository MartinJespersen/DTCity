
namespace wrapper
{
static R_SamplerInfo
SamplerFromCgltfSampler(CgltfSampler sampler)
{
    R_Filter min_filter = R_Filter_Nearest;
    R_Filter mag_filter = R_Filter_Nearest;
    R_MipMapMode mipmap_mode = R_MipMapMode_Nearest;
    R_SamplerAddressMode address_mode_u = R_SamplerAddressMode_Repeat;
    R_SamplerAddressMode address_mode_v = R_SamplerAddressMode_Repeat;

    switch (sampler.min_filter)
    {
        default: exit_with_error("Invalid min filter type"); break;
        case cgltf_filter_type_nearest:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Nearest;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            min_filter = R_Filter_Nearest;
            mag_filter = R_Filter_Nearest;
            mipmap_mode = R_MipMapMode_Linear;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            min_filter = R_Filter_Linear;
            mag_filter = R_Filter_Linear;
            mipmap_mode = R_MipMapMode_Linear;
        }
        break;
    }

    if (sampler.mag_filter != sampler.min_filter)
    {
        switch (sampler.mag_filter)
        {
            default: exit_with_error("Invalid mag filter type"); break;
            case cgltf_filter_type_nearest_mipmap_nearest:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_nearest:
            {
                mag_filter = R_Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest_mipmap_linear:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear_mipmap_linear:
            {
                mag_filter = R_Filter_Linear;
            }
            break;
            case cgltf_filter_type_nearest:
            {
                mag_filter = R_Filter_Nearest;
            }
            break;
            case cgltf_filter_type_linear:
            {
                mag_filter = R_Filter_Linear;
            }
            break;

                break;
        }
    }

    switch (sampler.wrap_s)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_u = R_SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_u = R_SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_u = R_SamplerAddressMode_MirroredRepeat;
            break;
    }
    switch (sampler.wrap_t)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            address_mode_v = R_SamplerAddressMode_ClampToEdge;
            break;
        case cgltf_wrap_mode_repeat: address_mode_v = R_SamplerAddressMode_Repeat; break;
        case cgltf_wrap_mode_mirrored_repeat:
            address_mode_v = R_SamplerAddressMode_MirroredRepeat;
            break;
    }

    R_SamplerInfo sampler_info = {.min_filter = min_filter,
                                  .mag_filter = mag_filter,
                                  .mip_map_mode = mipmap_mode,
                                  .address_mode_u = address_mode_u,
                                  .address_mode_v = address_mode_v};

    return sampler_info;
}

static CgltfNode*
ChildrenNodesDepthFirstPreOrder(Arena* arena, CgltfNode** stack, CgltfNode* node)
{
    CgltfNode* next = {0};
    if (node->node && node->node->children)
    {
        next = PushStruct(arena, CgltfNode);
        SLLStackPush(*stack, node);
        next->node = *node->node->children;
    }
    else
        for (CgltfNode* cur = node; cur && (*stack); cur = *stack, SLLStackPop(*stack))
        {
            if (cur)
            {
                if ((*stack))
                {
                    CgltfNode* parent = (*stack);
                    U32 next_index = parent->cur_child_index + 1;
                    if (next_index < parent->node->children_count)
                    {
                        next = PushStruct(arena, CgltfNode);
                        next->node = parent->node->children[next_index];

                        parent->cur_child_index = next_index;
                        break;
                    }
                }
            }
        }
    return next;
}
static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path, String8 root_node_name)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    cgltf_options options = {};
    cgltf_data* data = NULL;

    cgltf_accessor* accessor_position = NULL;
    cgltf_accessor* accessor_uv = NULL;

    cgltf_result result = cgltf_parse_file(&options, (char*)gltf_path.str, &data);
    Assert(result == cgltf_result_success);
    result = cgltf_load_buffers(&options, data, (char*)gltf_path.str);
    Assert(result == cgltf_result_success);

    cgltf_node* root_node = {0};
    // find root mesh from input argument
    for (U32 node_idx = 0; node_idx < data->nodes_count; node_idx += 1)
    {
        if (CStrEqual(data->nodes[node_idx].name, (char*)root_node_name.str))
        {
            root_node = &data->nodes[node_idx];
            break;
        }
    }

    struct BufferNodeList
    {
        BufferNode* first;
        BufferNode* last;
    };
    BufferNodeList buffer_node_list = {};
    AssertAlways(root_node);
    CgltfNode* first_node = PushStruct(scratch.arena, CgltfNode);
    first_node->node = root_node;
    first_node->cur_child_index = 0;
    CgltfNode* node_stack = {0};

    for (CgltfNode* cur_node = first_node; cur_node;
         cur_node = ChildrenNodesDepthFirstPreOrder(scratch.arena, &node_stack, cur_node))
    {
        cgltf_node* node = cur_node->node;
        cgltf_mesh* mesh = node->mesh;
        if (mesh)
        {
            // TODO: This is not optimal error handling
            Assert(mesh->primitives_count == 1);
            for (U32 prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx)
            {
                cgltf_primitive* primitive = &mesh->primitives[prim_idx];
                // loop to make sure indices between attributes are the same
                U32 expected_count = (U32)primitive->attributes[0].data->count;
                for (size_t i = 1; i < primitive->attributes_count; ++i)
                {
                    Assert(primitive->attributes[i].data->count == expected_count);
                }

                for (size_t i = 0; i < primitive->attributes_count; ++i)
                {
                    cgltf_attribute* attr = &primitive->attributes[i];

                    switch (attr->type)
                    {
                        case cgltf_attribute_type_position: accessor_position = attr->data; break;
                        case cgltf_attribute_type_texcoord:
                            if (attr->index == 0) // TEXCOORD_0
                                accessor_uv = attr->data;
                            break;
                        default: break;
                    }
                }

                cgltf_accessor* accessor_indices = primitive->indices;
                BufferNode* buffer_node = PushStruct(scratch.arena, BufferNode);
                SLLQueuePush(buffer_node_list.first, buffer_node_list.last, buffer_node);
                buffer_node->index_buffer = BufferAlloc<U32>(arena, accessor_indices->count);
                buffer_node->vertex_buffer = BufferAlloc<city::Vertex3D>(arena, expected_count);
                Buffer<city::Vertex3D>* vertex_buffer = &buffer_node->vertex_buffer;
                Buffer<U32>* index_buffer = &buffer_node->index_buffer;
                for (U32 indice_idx = 0; indice_idx < accessor_indices->count; indice_idx++)
                {
                    U32 index = (U32)cgltf_accessor_read_index(accessor_indices, indice_idx);
                    index_buffer->data[indice_idx] = index;
                }

                for (U32 vertex_idx = 0; vertex_idx < vertex_buffer->size; vertex_idx++)
                {
                    if (accessor_position)
                        cgltf_accessor_read_float(accessor_position, vertex_idx,
                                                  vertex_buffer->data[vertex_idx].pos.v, 3);
                    if (accessor_uv)
                        cgltf_accessor_read_float(accessor_uv, vertex_idx,
                                                  vertex_buffer->data[vertex_idx].uv.v, 2);
                }
            }
        }
    }

    cgltf_sampler* sampler = data->samplers;
    Assert(sampler);

    CgltfSampler cgltf_sampler = {
        .mag_filter = sampler->mag_filter,
        .min_filter = sampler->min_filter,
        .wrap_s = sampler->wrap_s,
        .wrap_t = sampler->wrap_t,
    };

    cgltf_free(data);

    U32 total_vertex_buffer_count = 0;
    U32 total_index_buffer_count = 0;
    for (BufferNode* buffer_node = buffer_node_list.first; buffer_node;
         buffer_node = buffer_node->next)
    {
        total_vertex_buffer_count += buffer_node->vertex_buffer.size;
        total_index_buffer_count += buffer_node->index_buffer.size;
    }

    Buffer<city::Vertex3D> vertex_buffer =
        BufferAlloc<city::Vertex3D>(arena, total_vertex_buffer_count);
    Buffer<U32> index_buffer = BufferAlloc<U32>(arena, total_index_buffer_count);

    U32 cur_vertex_buffer_idx = 0;
    U32 cur_index_buffer_idx = 0;
    for (BufferNode* buffer_node = buffer_node_list.first; buffer_node;
         buffer_node = buffer_node->next)
    {
        for (U32 i = 0; i < buffer_node->index_buffer.size; ++i)
        {
            buffer_node->index_buffer.data[i] =
                buffer_node->index_buffer.data[i] + cur_index_buffer_idx;
        }
        BufferCopy(vertex_buffer, buffer_node->vertex_buffer, cur_vertex_buffer_idx, 0,
                   buffer_node->vertex_buffer.size);
        BufferCopy(index_buffer, buffer_node->index_buffer, cur_index_buffer_idx, 0,
                   buffer_node->index_buffer.size);

        cur_vertex_buffer_idx += buffer_node->vertex_buffer.size;
        cur_index_buffer_idx += buffer_node->index_buffer.size;
    }
    return CgltfResult{vertex_buffer, index_buffer, cgltf_sampler};
}
} // namespace wrapper
