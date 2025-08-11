#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

namespace wrapper
{
static VkSamplerCreateInfo
VkSamplerFromCgltfSampler(CgltfSampler sampler)
{
    VkFilter vk_min_filter = VK_FILTER_NEAREST;
    VkFilter vk_mag_filter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode vk_address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode vk_address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    switch (sampler.min_filter)
    {
    case cgltf_filter_type_nearest:
    {
        vk_min_filter = VK_FILTER_NEAREST;
        vk_mag_filter = VK_FILTER_NEAREST;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    break;
    case cgltf_filter_type_linear:
    {
        vk_min_filter = VK_FILTER_LINEAR;
        vk_mag_filter = VK_FILTER_LINEAR;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    break;
    case cgltf_filter_type_nearest_mipmap_nearest:
    {
        vk_min_filter = VK_FILTER_NEAREST;
        vk_mag_filter = VK_FILTER_NEAREST;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    break;
    case cgltf_filter_type_linear_mipmap_nearest:
    {
        vk_min_filter = VK_FILTER_LINEAR;
        vk_mag_filter = VK_FILTER_LINEAR;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    break;
    case cgltf_filter_type_nearest_mipmap_linear:
    {
        vk_min_filter = VK_FILTER_NEAREST;
        vk_mag_filter = VK_FILTER_NEAREST;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    break;
    case cgltf_filter_type_linear_mipmap_linear:
    {
        vk_min_filter = VK_FILTER_LINEAR;
        vk_mag_filter = VK_FILTER_LINEAR;
        vk_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    break;
    }

    if (sampler.mag_filter != sampler.min_filter)
    {
        switch (sampler.mag_filter)
        {
        case cgltf_filter_type_nearest_mipmap_nearest:
        {
            vk_mag_filter = VK_FILTER_NEAREST;
        }
        break;
        case cgltf_filter_type_linear_mipmap_nearest:
        {
            vk_mag_filter = VK_FILTER_LINEAR;
        }
        break;
        case cgltf_filter_type_nearest_mipmap_linear:
        {
            vk_mag_filter = VK_FILTER_NEAREST;
        }
        break;
        case cgltf_filter_type_linear_mipmap_linear:
        {
            vk_mag_filter = VK_FILTER_LINEAR;
        }
        break;
        case cgltf_filter_type_nearest:
        {
            vk_mag_filter = VK_FILTER_NEAREST;
        }
        break;
        case cgltf_filter_type_linear:
        {
            vk_mag_filter = VK_FILTER_LINEAR;
        }
        break;

            break;
        }
    }

    switch (sampler.wrap_s)
    {
    case cgltf_wrap_mode_clamp_to_edge:
        vk_address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    case cgltf_wrap_mode_repeat:
        vk_address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    case cgltf_wrap_mode_mirrored_repeat:
        vk_address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    }
    switch (sampler.wrap_t)
    {
    case cgltf_wrap_mode_clamp_to_edge:
        vk_address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    case cgltf_wrap_mode_repeat:
        vk_address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    case cgltf_wrap_mode_mirrored_repeat:
        vk_address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    }
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = vk_min_filter;
    samplerInfo.minFilter = vk_mag_filter;
    samplerInfo.addressModeU = vk_address_mode_u;
    samplerInfo.addressModeV = vk_address_mode_v;
    samplerInfo.addressModeW = samplerInfo.addressModeU;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = vk_mipmap_mode;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    return samplerInfo;
}

static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path)
{
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    cgltf_accessor* accessor_position = NULL;
    cgltf_accessor* accessor_uv = NULL;

    Buffer<city::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;

    cgltf_result result = cgltf_parse_file(&options, (char*)gltf_path.str, &data);
    Assert(result == cgltf_result_success);
    result = cgltf_load_buffers(&options, data, (char*)gltf_path.str);
    Assert(result == cgltf_result_success);

    cgltf_mesh* mesh = &data->meshes[0];
    for (U32 prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx)
    {
        cgltf_primitive* primitive = &mesh->primitives[prim_idx];

        U32 expected_count = primitive->attributes[0].data->count;
        for (size_t i = 1; i < primitive->attributes_count; ++i)
        {
            if (primitive->attributes[i].data->count != expected_count)
            {
                fprintf(stderr,
                        "Non-shared indexing detected: attribute %zu has %zu entries (expected "
                        "%zu)\n",
                        i, primitive->attributes[i].data->count, expected_count);
                exit(1); // or handle with deduplication logic
            }
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
            U32 index = cgltf_accessor_read_index(accessor_indices, indice_idx);
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
