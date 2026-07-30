namespace draw
{

g_internal Draw g_draw_ctx = {};

g_internal void
draw_init()
{
    if (!g_draw_ctx.frame_arena)
    {
        g_draw_ctx.frame_arena = arena_alloc();
        Debug_SetName(g_draw_ctx.frame_arena, "draw frame arena");
    }
}

g_internal void
draw_release()
{
    if (g_draw_ctx.frame_arena)
    {
        arena_release(g_draw_ctx.frame_arena);
    }
    g_draw_ctx = {};
}

g_internal void
draw_new_frame()
{
    arena_clear(g_draw_ctx.frame_arena);
    g_draw_ctx.frame = PushStruct(g_draw_ctx.frame_arena, DrawFrame);
}

g_internal Arena*
draw_frame_arena_get()
{
    return g_draw_ctx.frame_arena;
}

g_internal DrawFrame*
draw_frame_get()
{
    if (!g_draw_ctx.frame)
    {
        draw_new_frame();
    }
    return g_draw_ctx.frame;
}

} // namespace draw
