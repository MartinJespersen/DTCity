////////////////////////////////
//~mgj: Container

template <typename T>
Buffer<T>
BufferAlloc(Arena* arena, U64 count)
{
    Buffer<T> buffer = {0};
    buffer.data = PushArray(arena, T, count);
    buffer.size = count;
    return buffer;
};

////////////////////////////////
static Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings)
{
    Buffer<String8> buffer = BufferAlloc<String8>(arena, strings.size());
    U32 index = 0;
    for (const char* const* str = strings.begin(); str != strings.end(); ++str)
    {
        buffer.data[index++] = PushStr8Copy(arena, Str8CString(*str));
    }
    return buffer;
}

static String8
Str8PathFromStr8List(Arena* arena, std::initializer_list<String8> strings)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = OS_PathDelimiter()};
    for (const String8* str = strings.begin(); str != strings.end(); ++str)
    {
        Str8ListPush(arena, &path_list, *str);
    }
    String8 result = Str8ListJoin(arena, &path_list, &join_params);
    return result;
}

static String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = OS_PathDelimiter()};

    // Step 1: Convert each char* to String8 and push to list
    for (U64 i = 0; i < path_elements.size; i++)
    {
        String8 part = path_elements.data[i];
        Str8ListPush(arena, &path_list, part);
    }

    String8 result = Str8ListJoin(arena, &path_list, &join_params);
    return result;
}

static Buffer<U8>
IO_ReadFile(Arena* arena, String8 filename)
{
    Buffer<U8> buffer = {0};
    FILE* file = fopen((const char*)filename.str, "rb");
    if (file == NULL)
    {
        exitWithError("failed to open file!");
    }

    fseek(file, 0, SEEK_END);
    buffer.size = (U64)ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer.data = PushArray(arena, U8, buffer.size);
    fread(buffer.data, sizeof(U8), buffer.size, file);

    fclose(file);
    return buffer;
}

//~mgj: Strings functions

static char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer)
{
    char** arr = PushArray(arena, char*, buffer.size);

    for (U32 i = 0; i < buffer.size; i++)
    {
        arr[i] = (char*)buffer.data[i].str;
    }
    return arr;
}
