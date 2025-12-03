////////////////////////////////////////////////////////
// ~mgj: Handles
static r_Handle
r_handle_zero()
{
    r_Handle handle = {};
    return handle;
}

////////////////////////////////////////////////////////
template <typename T>
static r_BufferInfo
r_buffer_info_from_template_buffer(Buffer<T> buffer, r_BufferType buffer_type)
{
    U64 type_size = sizeof(T);
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size, .buffer_type = buffer_type};
}

static bool
r_is_handle_zero(r_Handle handle)
{
    return handle.ptr == 0;
}
