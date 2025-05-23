internal Buffer<U8>
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

    buffer.data = push_array(arena, U8, buffer.size);
    fread(buffer.data, sizeof(U8), buffer.size, file);

    fclose(file);
    return buffer;
}