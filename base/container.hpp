////////////////////////////////

// ~mgj: foreign includes
#include <initializer_list>

//~ mgj: Container
template <typename T> struct Buffer
{
    T* data;
    U64 size;
};

template <typename T>
Buffer<T>
BufferAlloc(Arena* arena, U64 count);

//~mgj: container creation
static Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings);
static String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements);

// ~mgj: io
static Buffer<U8>
IO_ReadFile(Arena* arena, String8 filename);
