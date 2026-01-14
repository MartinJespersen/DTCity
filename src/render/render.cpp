namespace render
{
////////////////////////////////////////////////////////
// ~mgj: Handles
static render::Handle
handle_zero()
{
    render::Handle handle = {};
    return handle;
}

////////////////////////////////////////////////////////
static render::BufferInfo
buffer_info_from_vertex_3d_buffer(Buffer<Vertex3D> buffer, render::BufferType buffer_type)
{
    U64 type_size = sizeof(Vertex3D);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_vertex_blend_3d_buffer(Buffer<Vertex3DBlend> buffer,
                                        render::BufferType buffer_type)
{
    U64 type_size = sizeof(Vertex3DBlend);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_u32_index_buffer(Buffer<U32> buffer, render::BufferType buffer_type)
{
    U64 type_size = sizeof(U32);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_vertex_3d_instance_buffer(Buffer<render::Model3DInstance> buffer,
                                           render::BufferType buffer_type)
{
    U64 type_size = sizeof(render::Model3DInstance);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static bool
is_handle_zero(render::Handle handle)
{
    return handle.ptr == 0;
}

// privates
template <typename T>
static render::BufferInfo
buffer_info_from_template_buffer(Buffer<T> buffer, render::BufferType buffer_type, U64 type_size)
{
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size, .buffer_type = buffer_type};
}

} // namespace render
