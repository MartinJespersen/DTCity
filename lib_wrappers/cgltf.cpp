
namespace wrapper
{
static SamplerInfo
SamplerFromCgltfSampler(CgltfSampler sampler)
{
    Filter min_filter = Filter_Nearest;
    Filter mag_filter = Filter_Nearest;
    MipMapMode mipmap_mode = MipMapMode_Nearest;
    SamplerAddressMode address_mode_u = SamplerAddressMode_Repeat;
    SamplerAddressMode address_mode_v = SamplerAddressMode_Repeat;

    switch (sampler.min_filter)
    {
    case cgltf_filter_type_nearest:
    {
        min_filter = Filter_Nearest;
        mag_filter = Filter_Nearest;
        mipmap_mode = MipMapMode_Nearest;
    }
    break;
    case cgltf_filter_type_linear:
    {
        min_filter = Filter_Linear;
        mag_filter = Filter_Linear;
        mipmap_mode = MipMapMode_Nearest;
    }
    break;
    case cgltf_filter_type_nearest_mipmap_nearest:
    {
        min_filter = Filter_Nearest;
        mag_filter = Filter_Nearest;
        mipmap_mode = MipMapMode_Nearest;
    }
    break;
    case cgltf_filter_type_linear_mipmap_nearest:
    {
        min_filter = Filter_Linear;
        mag_filter = Filter_Linear;
        mipmap_mode = MipMapMode_Nearest;
    }
    break;
    case cgltf_filter_type_nearest_mipmap_linear:
    {
        min_filter = Filter_Nearest;
        mag_filter = Filter_Nearest;
        mipmap_mode = MipMapMode_Linear;
    }
    break;
    case cgltf_filter_type_linear_mipmap_linear:
    {
        min_filter = Filter_Linear;
        mag_filter = Filter_Linear;
        mipmap_mode = MipMapMode_Nearest;
    }
    break;
    }

    if (sampler.mag_filter != sampler.min_filter)
    {
        switch (sampler.mag_filter)
        {
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            mag_filter = Filter_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            mag_filter = Filter_Linear;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            mag_filter = Filter_Nearest;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            mag_filter = Filter_Linear;
        }
        break;
        case cgltf_filter_type_nearest:
        {
            mag_filter = Filter_Nearest;
        }
        break;
        case cgltf_filter_type_linear:
        {
            mag_filter = Filter_Linear;
        }
        break;

            break;
        }
    }

    switch (sampler.wrap_s)
    {
    case cgltf_wrap_mode_clamp_to_edge:
        address_mode_u = SamplerAddressMode_ClampToEdge;
        break;
    case cgltf_wrap_mode_repeat:
        address_mode_u = SamplerAddressMode_Repeat;
        break;
    case cgltf_wrap_mode_mirrored_repeat:
        address_mode_u = SamplerAddressMode_MirroredRepeat;
        break;
    }
    switch (sampler.wrap_t)
    {
    case cgltf_wrap_mode_clamp_to_edge:
        address_mode_v = SamplerAddressMode_ClampToEdge;
        break;
    case cgltf_wrap_mode_repeat:
        address_mode_v = SamplerAddressMode_Repeat;
        break;
    case cgltf_wrap_mode_mirrored_repeat:
        address_mode_v = SamplerAddressMode_MirroredRepeat;
        break;
    }

    SamplerInfo sampler_info = {.min_filter = min_filter,
                                .mag_filter = mag_filter,
                                .mip_map_mode = mipmap_mode,
                                .address_mode_u = address_mode_u,
                                .address_mode_v = address_mode_v};

    return sampler_info;
}

static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path)
{
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    cgltf_accessor* accessor_position = NULL;
    cgltf_accessor* accessor_uv = NULL;

    Buffer<city::CarVertex> vertex_buffer = {0};
    Buffer<U32> index_buffer = {0};

    cgltf_result result = cgltf_parse_file(&options, (char*)gltf_path.str, &data);
    Assert(result == cgltf_result_success);
    result = cgltf_load_buffers(&options, data, (char*)gltf_path.str);
    Assert(result == cgltf_result_success);

    cgltf_mesh* mesh = &data->meshes[0];
    for (U32 prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx)
    {
        cgltf_primitive* primitive = &mesh->primitives[prim_idx];

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
            case cgltf_attribute_type_position:
                accessor_position = attr->data;
                break;
            case cgltf_attribute_type_texcoord:
                if (attr->index == 0) // TEXCOORD_0
                    accessor_uv = attr->data;
                break;
            default:
                break;
            }
        }

        cgltf_accessor* accessor_indices = primitive->indices;
        index_buffer = BufferAlloc<U32>(arena, accessor_indices->count);
        for (U32 indice_idx = 0; indice_idx < accessor_indices->count; indice_idx++)
        {
            U32 index = (U32)cgltf_accessor_read_index(accessor_indices, indice_idx);
            index_buffer.data[indice_idx] = index;
        }

        vertex_buffer = BufferAlloc<city::CarVertex>(arena, expected_count);
        for (U32 vertex_idx = 0; vertex_idx < vertex_buffer.size; vertex_idx++)
        {
            if (accessor_position)
                cgltf_accessor_read_float(accessor_position, vertex_idx,
                                          vertex_buffer.data[vertex_idx].pos.v, 3);
            if (accessor_uv)
                cgltf_accessor_read_float(accessor_uv, vertex_idx,
                                          vertex_buffer.data[vertex_idx].uv, 2);
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

    return CgltfResult{vertex_buffer, index_buffer, cgltf_sampler};
}
} // namespace wrapper
