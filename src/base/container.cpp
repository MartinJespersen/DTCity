#include "diagnostics.hpp"
#include "base/base_inc.hpp"


////////////////////////////////
//~Array










//~mgj: Container








////////////////////////////////
Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings)
{
    Buffer<String8> buffer = buffer_alloc<String8>(arena, strings.size());
    U32 index = 0;
    for (const char* const* str = strings.begin(); str != strings.end(); ++str)
    {
        buffer.data[index++] = push_str8_copy(arena, str8_c_string(*str));
    }
    return buffer;
}

String8
str8_path_from_str8_list(Arena* arena, std::initializer_list<String8> strings)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = os_path_delimiter()};
    for (const String8* str = strings.begin(); str != strings.end(); ++str)
    {
        str8_list_push(arena, &path_list, *str);
    }
    String8 result = str8_list_join(arena, &path_list, &join_params);
    return result;
}

String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = os_path_delimiter()};

    // Step 1: Convert each char* to String8 and push to list
    for (U64 i = 0; i < path_elements.size; i++)
    {
        String8 part = path_elements.data[i];
        str8_list_push(arena, &path_list, part);
    }

    String8 result = str8_list_join(arena, &path_list, &join_params);
    return result;
}

namespace io
{

Buffer<U8>
file_read(Arena* arena, String8 filename)
{
    Buffer<U8> buffer = {0};
    FILE* file = fopen((const char*)filename.str, "rb");
    defer(fclose(file));
    Assert(file != nullptr);
    if (file == NULL)
    {
        DEBUG_LOG("failed to open file!");

        return buffer;
    }

    fseek(file, 0, SEEK_END);
    buffer.size = (U64)ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer.data = PushArray(arena, U8, buffer.size);
    fread(buffer.data, sizeof(U8), buffer.size, file);

    return buffer;
}

} // namespace io

//~mgj: Strings functions

char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer)
{
    char** arr = PushArray(arena, char*, buffer.size);

    for (U32 i = 0; i < buffer.size; i++)
    {
        arr[i] = (char*)buffer.data[i].str;
    }
    return arr;
}

//~mgj: ChunckList: safe T value in contigous Chunks usually for intermediate storage










String8
str8_from_chunk_list(Arena* arena, ChunkList<U8>* list)
{
    String8 buffer = push_str8_fill_byte(arena, list->total_count, 0);
    U64 offset = 0;
    for (ChunkItem<U8>* chunk = list->first; chunk; chunk = chunk->next)
    {
        MemoryCopy(buffer.str + offset, chunk->values, chunk->count * sizeof(U8));
        offset += chunk->count;
    }
    return buffer;
}
