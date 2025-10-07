////////////////////////////////////////////////////////
// ~mgj: Handles
static R_Handle
R_HandleZero()
{
    R_Handle handle = {};
    return handle;
}

////////////////////////////////////////////////////////
template <typename T>
static R_BufferInfo
R_BufferInfoFromTemplateBuffer(Buffer<T> buffer, R_BufferType buffer_type)
{
    U64 type_size = sizeof(T);
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size, .buffer_type = buffer_type};
}
