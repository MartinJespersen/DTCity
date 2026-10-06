#pragma once

// Template implementations
namespace render
{
template <typename T>
render::BufferInfo::BufferInfo(Buffer<T> buffer, U32 buffer_type)
{
    U64 byte_count = buffer.size * sizeof(T);
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    this->buffer = general_buffer;
    this->type_size = sizeof(T);
    this->buffer_type = buffer_type;
    this->elem_count = buffer.size;
}

template <typename T>
render::BufferInfo::BufferInfo(Arena* arena, T* input, U32 buffer_type)
{
    T* node = PushStruct(arena, T);
    *node = *input;
    Buffer<U8> general_buffer = {.data = (U8*)node, .size = sizeof(T)};
    this->buffer = general_buffer;
    this->type_size = sizeof(T);
    this->buffer_type = buffer_type;
    this->elem_count = 1;
}

template <typename T>
render::BufferInfo
render::BufferInfo::empty_buffer_info(Arena* arena, BufferType buffer_type)
{
    T* node = PushStruct(arena, T);
    Buffer<U8> buffer = {.data = (U8*)node, .size = sizeof(T)};
    render::BufferInfo buffer_info = render::BufferInfo(buffer, buffer_type);
    return buffer_info;
}
}
