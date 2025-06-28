#pragma once

template <typename T> struct Buffer
{
    T* data;
    U64 size;
};

template <typename T>
Buffer<T>
BufferAlloc(Arena* arena, U64 count)
{
    Buffer<T> buffer = {0};
    buffer.data = push_array(arena, T, count);
    buffer.size = count;
    return buffer;
};
