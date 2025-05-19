root_function Buffer
IO_ReadFile(Arena* arena, String8 filename)
{
    Buffer buffer = {0};
    FILE* file = fopen((const char*)filename.str, "rb");
    if (file == NULL)
    {
        exitWithError("failed to open file!");
    }

    fseek(file, 0, SEEK_END);
    buffer.size = (u64)ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer.data = PushArray(arena, u8, buffer.size);
    fread(buffer.data, sizeof(u8), buffer.size, file);

    fclose(file);
    return buffer;
}