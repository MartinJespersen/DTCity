////////////////////////////////////////////////////////
// ~mgj: Handles
static r_Handle
r_handle_zero()
{
    r_Handle handle = {};
    return handle;
}

////////////////////////////////////////////////////////
static r_BufferInfo
r_buffer_info_from_vertex_3d_buffer(Buffer<r_Vertex3D> buffer, r_BufferType buffer_type)
{
    U64 type_size = sizeof(r_Vertex3D);
    return r_buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static r_BufferInfo
r_buffer_info_from_u32_index_buffer(Buffer<U32> buffer, r_BufferType buffer_type)
{
    U64 type_size = sizeof(U32);
    return r_buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static r_BufferInfo
r_buffer_info_from_vertex_3d_instance_buffer(Buffer<r_Model3DInstance> buffer,
                                             r_BufferType buffer_type)
{
    U64 type_size = sizeof(r_Model3DInstance);
    return r_buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static bool
r_is_handle_zero(r_Handle handle)
{
    return handle.ptr == 0;
}

// privates
template <typename T>
static r_BufferInfo
r_buffer_info_from_template_buffer(Buffer<T> buffer, r_BufferType buffer_type, U64 type_size)
{
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size, .buffer_type = buffer_type};
}
