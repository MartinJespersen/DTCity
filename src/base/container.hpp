////////////////////////////////

// ~mgj: foreign includes
#include <initializer_list>

// ~mgj: Result type
template <typename T> struct Result
{
    T v;
    B32 err;
};

//~ mgj: Container
template <typename T> struct Buffer
{
    T* data;
    U64 size;
};

template <typename T> struct BufferGrowable
{
    Buffer<T> buffer;
    U64 index;
};

template <typename T>
Buffer<T>
BufferAlloc(Arena* arena, U64 count);
template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 element_count_to_copy);
template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 dst_offset, U64 src_offset, U64 size);
template <typename T>
static void
BufferItemRemove(Buffer<T>* in_out_buffer, U32 index);
template <typename T>
static void
BufferAppend(Buffer<T> buffer, Buffer<T> buffer_append, U32 append_idx);

//~mgj: container creation
static Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings);
static String8
Str8PathFromStr8List(Arena* arena, std::initializer_list<String8> strings);
static String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements);

// ~mgj: io
static Buffer<U8>
io_file_read(Arena* arena, String8 filename);

// ~mgj: String
static char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer);
