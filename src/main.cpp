#include "includes.hpp"
#include "includes.cpp"

static Context*
ContextCreate(io_IO* io_ctx)
{
    HTTP_Init();

    ScratchScope scratch = ScratchScope(0, 0);
    //~mgj: app context setup
    Arena* app_arena = (Arena*)ArenaAlloc();
    Context* ctx = PushStruct(app_arena, Context);
    ctx->arena_permanent = app_arena;
    ctx->io = PushStruct(app_arena, io_IO);
    ctx->camera = PushStruct(app_arena, ui_Camera);
    ctx->time = PushStruct(app_arena, dt_Time);
    ctx->cwd = Str8PathFromStr8List(app_arena, {OS_GetCurrentPath(scratch.arena), S("..")});
    ctx->data_dir = Str8PathFromStr8List(app_arena, {ctx->cwd, S("data")});

    // ctx->sub_paths = Str8PathFromStr8List(app_arena, {ctx->data_dir, S("sub")});
    dt_DataDirPair subdirs[] = {{dt_DataDirType::Cache, S("cache")},
                                {dt_DataDirType::Texture, S("textures")},
                                {dt_DataDirType::Shaders, S("shaders")},
                                {dt_DataDirType::Assets, S("assets")}};
    ctx->data_subdir = dt_dir_create(app_arena, ctx->data_dir, subdirs, ArrayCount(subdirs));
    ctx->io = io_ctx;

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 10; // TODO: should be increased
    ctx->thread_pool = async::WorkerThreadsCreate(app_arena, thread_count, queue_size);

    return ctx;
}

static void
ContextDestroy(Context* ctx)
{
    // TODO: This should probably be uncommented
    // async::WorkerThreadDestroy(ctx->thread_pool);
    ArenaRelease(ctx->arena_permanent);
}

void
App(int argc, char** argv)
{
    ScratchScope scratch = ScratchScope(0, 0);

    io_IO* io_ctx = io_window_create(VK_Context::WIDTH, VK_Context::HEIGHT);
    io_input_state_update(io_ctx);
    Context* ctx = ContextCreate(io_ctx);
    dt_ctx_set(ctx);
    dt_time_init(ctx->time);
    dt_Input input = dt_interpret_input(argc, argv);

    OS_Handle thread_handle = dt_render_thread_start(ctx, &input);
    while (!glfwWindowShouldClose(ctx->io->window))
    {
        io_input_state_update(ctx->io);
    }
    dt_render_thread_join(thread_handle, ctx);
    ContextDestroy(ctx);
    io_window_destroy(io_ctx);
}
