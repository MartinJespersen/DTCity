#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

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

g_internal CgltfResult
gltfw_gltf_read(Arena* arena, String8 gltf_path, String8 root_node_name)
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
                buffer_node->vertex_buffer = BufferAlloc<gltfw_Vertex3D>(arena, expected_count);
                Buffer<gltfw_Vertex3D>* vertex_buffer = &buffer_node->vertex_buffer;
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

    gltfw_Sampler cgltf_sampler = {
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

    Buffer<gltfw_Vertex3D> vertex_buffer =
        BufferAlloc<gltfw_Vertex3D>(arena, total_vertex_buffer_count);
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

g_internal gltfw_Primitive*
gltfw_primitive_create(Arena* arena, cgltf_data* data, cgltf_primitive* in_prim)
{
    gltfw_Primitive* primitive = PushStruct(arena, gltfw_Primitive);

    cgltf_accessor* accessor_position = {};
    cgltf_accessor* accessor_uv = {};

    U32 expected_count = (U32)in_prim->attributes[0].data->count;
    for (size_t i = 1; i < in_prim->attributes_count; ++i)
    {
        Assert(in_prim->attributes[i].data->count == expected_count);
    }

    for (size_t i = 0; i < in_prim->attributes_count; ++i)
    {
        cgltf_attribute* attr = &in_prim->attributes[i];

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

    // get texture
    cgltf_material* material = in_prim->material;
    cgltf_pbr_metallic_roughness* pbr_material = &material->pbr_metallic_roughness;
    cgltf_texture_view* texture_view = &pbr_material->base_color_texture;
    primitive->tex_idx = (U32)cgltf_texture_index(data, texture_view->texture);

    cgltf_accessor* accessor_indices = in_prim->indices;
    primitive->indices = BufferAlloc<U32>(arena, accessor_indices->count);
    primitive->vertices = BufferAlloc<gltfw_Vertex3D>(arena, expected_count);
    for (U32 indice_idx = 0; indice_idx < accessor_indices->count; indice_idx++)
    {
        U32 index = (U32)cgltf_accessor_read_index(accessor_indices, indice_idx);
        primitive->indices.data[indice_idx] = index;
    }

    for (U32 vertex_idx = 0; vertex_idx < primitive->vertices.size; vertex_idx++)
    {
        if (accessor_position)
            cgltf_accessor_read_float(accessor_position, vertex_idx,
                                      primitive->vertices.data[vertex_idx].pos.v, 3);
        if (accessor_uv)
            cgltf_accessor_read_float(accessor_uv, vertex_idx,
                                      primitive->vertices.data[vertex_idx].uv.v, 2);
    }

    return primitive;
}

g_internal gltfw_PrimitiveList
gltfw_primitives_read(Arena* arena, cgltf_data* data)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    gltfw_PrimitiveList prim_list = {};

    for (U32 scene_idx = 0; scene_idx < data->scenes_count; ++scene_idx)
    {
        cgltf_scene* scene = &data->scenes[scene_idx];
        CgltfNode* first_node = PushStruct(scratch.arena, CgltfNode);
        for (U32 root_node_idx = 0; root_node_idx < scene->nodes_count; ++root_node_idx)
        {
            first_node->node = scene->nodes[root_node_idx];
            first_node->cur_child_index = 0;
            CgltfNode* node_stack = {0};

            for (CgltfNode* cur_node = first_node; cur_node;
                 cur_node = ChildrenNodesDepthFirstPreOrder(scratch.arena, &node_stack, cur_node))
            {
                cgltf_node* node = cur_node->node;
                cgltf_mesh* mesh = node->mesh;
                if (mesh)
                {
                    for (U32 prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx)
                    {
                        cgltf_primitive* primitive = &mesh->primitives[prim_idx];
                        gltfw_Primitive* primitive_w =
                            gltfw_primitive_create(arena, data, primitive);
                        SLLQueuePush(prim_list.first, prim_list.last, primitive_w);
                    }
                }
            }
        }
    }
    return prim_list;
}

g_internal Buffer<gltfw_Texture>
gltfw_textures_read(Arena* arena, cgltf_data* data)
{
    Buffer<gltfw_Texture> textures = BufferAlloc<gltfw_Texture>(arena, data->textures_count);
    for (U32 tex_idx = 0; tex_idx < textures.size; ++tex_idx)
    {
        gltfw_Texture* tex = &textures.data[tex_idx];
        cgltf_texture* cgltf_tex = &data->textures[tex_idx];
        cgltf_sampler* cgltf_sampler = cgltf_tex->sampler;

        gltfw_Sampler gltfw_sampler = {
            .mag_filter = cgltf_sampler->mag_filter,
            .min_filter = cgltf_sampler->min_filter,
            .wrap_s = cgltf_sampler->wrap_s,
            .wrap_t = cgltf_sampler->wrap_t,
        };

        tex->sampler = gltfw_sampler;

        cgltf_buffer_view* buf_view = cgltf_tex->image->buffer_view;
        cgltf_buffer* c_buf = buf_view->buffer;
        U8* buf = (U8*)c_buf->data + buf_view->offset;
        tex->tex_buf = {buf, buf_view->size};
    }

    return textures;
}

static void*
gltfw_alloc_func(void* user, size_t size)
{
    Arena* arena = (Arena*)user;
    void* ptr = PushArray(arena, U8, size);
    return ptr;
}

static void
gltfw_free_func(void* user, void* ptr)
{
    (void)user;
    (void)ptr;
}

static gltfw_Result
gltfw_glb_read(Arena* arena, String8 glb_path)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    cgltf_options options = {};
    cgltf_data* data = NULL;

    options.memory.user_data = arena;
    options.memory.alloc_func = gltfw_alloc_func;
    options.memory.free_func = gltfw_free_func;
    cgltf_result result = cgltf_parse_file(&options, (char*)glb_path.str, &data);
    Assert(result == cgltf_result_success);

    // use custom allocator for buffers as there lifetime is managed by the arena
    result = cgltf_load_buffers(&options, data, (char*)glb_path.str);
    Assert(result == cgltf_result_success);

    gltfw_PrimitiveList prim_list = gltfw_primitives_read(arena, data);
    Buffer<gltfw_Texture> tex_buffer = gltfw_textures_read(arena, data);

    return gltfw_Result{prim_list, tex_buffer};
}
