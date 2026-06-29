#include "includes.hpp"
#include "includes.cpp"

static Context*
ctx_create(io::IO* io_ctx)
{
    ScratchScope scratch = ScratchScope(0, 0);
    //~mgj: app context setup
    Arena* app_arena = (Arena*)arena_alloc();
    Debug_SetName(app_arena, "app arena");
    Context* ctx = PushStruct(app_arena, Context);
    ctx->arena_main_permanent = app_arena;
    ctx->arena_frame = arena_alloc();
    Debug_SetName(ctx->arena_frame, "app frame arena");
    ctx->io = PushStruct(app_arena, io::IO);

    ctx->camera_container = resource_pool_init<ui::Camera>(10);
    ctx->time = PushStruct(app_arena, dt_Time);
    ctx->cwd = os_current_path_get(scratch.arena);

    String8 data_dir = str8_path_from_str8_list(scratch.arena, {ctx->cwd, S("data")});
    {
        U32 retry_count = 0;
        String8List parent_dir_list = {};
        const U32 retry_stop = 4;
        for (; retry_count < retry_stop && os_folder_path_exists(data_dir) == false; retry_count++)
        {
            str8_list_push(scratch.arena, &parent_dir_list, S(".."));
            StringJoin join_params = {.sep = os_path_delimiter()};
            String8 parent_path = str8_list_join(scratch.arena, &parent_dir_list, &join_params);
            data_dir = str8_path_from_str8_list(scratch.arena, {ctx->cwd, parent_path, S("data")});
        }
        if (retry_count >= retry_stop)
        {
            exit_with_error("data directory cannot be found");
        }
    }
    ctx->data_dir = push_str8_copy(app_arena, data_dir);

    dt_DataDirPair subdirs[] = {{dt_DataDirType::Cache, S("cache")}, {dt_DataDirType::Texture, S("textures")}, {dt_DataDirType::Shaders, S("shaders")}, {dt_DataDirType::Assets, S("assets")}};
    ctx->data_subdirs = dt_dir_create(app_arena, data_dir, subdirs, ArrayCount(subdirs));
    ctx->io = io_ctx;

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 100; // TODO: should be increased
    U32 main_thread_queue_size = 10;
    ctx->thread_pool = async::thread_pool_create(app_arena, thread_count, queue_size, main_thread_queue_size);

    return ctx;
}

static void
ctx_destroy(Context* ctx)
{
    async::thread_pool_destroy(ctx->thread_pool);
    resource_pool_release(ctx->camera_container);
    arena_release(ctx->arena_main_permanent);
}

int
App(int argc, char** argv)
{
    ScratchScope scratch = ScratchScope(0, 0);
    dynamic_array_init();

    io::IO* io_ctx = io::window_create(S("Digital Twin City"), vulkan::Context::WIDTH, vulkan::Context::HEIGHT);
    io::input_state_update(io_ctx);

    Context* ctx = ctx_create(io_ctx);
    dt_ctx_set(ctx);
    ctx->cmdline = os_parse_cmd_line(ctx->arena_main_permanent, argc, argv);
    dt_time_init(ctx->time);

    OS_Handle thread_handle = dt_render_thread_start(ctx);
    while (!glfwWindowShouldClose(ctx->io->window))
    {
        io::input_state_update(ctx->io);
    }
    dt_render_thread_join(thread_handle, ctx);
    io::window_destroy(io_ctx);
    arena_release(ctx->arena_frame);
    ctx_destroy(ctx);
    return 0;
}
