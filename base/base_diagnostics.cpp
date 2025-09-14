static void
WriteToLog(const char* file_name, const char* message, ...)
{
    ScratchScope scratch = ScratchScope(0, 0);
    String8 result = {0};
    va_list args;
    va_start(args, message);
    result = PushStr8FV(scratch.arena, message, args);
    va_end(args);
    FILE* file = fopen((char*)file_name, "a");
    if (file == NULL)
    {
        exitWithError("Failed to open log file");
    }
    fprintf(file, "%s\n", (char*)message);
    fclose(file);
}
