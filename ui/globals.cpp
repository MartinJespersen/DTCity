// Global Context
Context *g_ctx;
UI_Widget* g_ui_widget;
ThreadCtx* g_thread_ctx;

no_name_mangle void GlobalContextSet(Context* ctx) {
    g_ctx = ctx;
    g_ui_widget = ctx->g_ui_widget;
    g_thread_ctx = ctx->thread_ctx;
}

inline_function Context* GlobalContextGet() {
    return g_ctx;    
}

//Thread Context ------------------------------------------------------------

root_function ThreadCtx* 
ThreadCtxGet(){
    return g_thread_ctx;
}

root_function void
ThreadContextInit()
{
    u64 size = GIGABYTE(4);
    for (u32 tctx_i = 0; tctx_i < ArrayCount(g_thread_ctx->scratchArenas); tctx_i++)
    {
        g_thread_ctx->scratchArenas[tctx_i] = ArenaAlloc(size);
    }
}

root_function void
ThreadContextExit()
{
    for (u32 tctx_i = 0; tctx_i < ArrayCount(g_thread_ctx->scratchArenas); tctx_i++)
    {
        ArenaDealloc(g_thread_ctx->scratchArenas[tctx_i]);
    }
}

root_function ArenaTemp
ArenaScratchGet()
{
    ArenaTemp temp = {};
    temp.pos = g_thread_ctx->scratchArenas[0]->pos;
    temp.arena = g_thread_ctx->scratchArenas[0];
    return temp;
}

root_function ArenaTemp
ArenaScratchGet(Arena** conflicts, u64 conflict_count)
{
    ArenaTemp scratch = {0};
    ThreadCtx* tctx = g_thread_ctx;
    for (u64 tctx_idx = 0; tctx_idx < ArrayCount(tctx->scratchArenas); tctx_idx += 1)
    {
        b32 is_conflicting = 0;
        for (Arena** conflict = conflicts; conflict < conflicts + conflict_count; conflict += 1)
        {
            if (*conflict == tctx->scratchArenas[tctx_idx])
            {
                is_conflicting = 1;
                break;
            }
        }
        if (is_conflicting == 0)
        {
            scratch.arena = tctx->scratchArenas[tctx_idx];
            scratch.pos = scratch.arena->pos;
            break;
        }
    }
    return scratch;
}

// Widget configuration ----------------------------------------------------------
#define X(name, type, default) \
    inline_function type name##_Get() \
    { \
        Context* ctx = GlobalContextGet(); \
        UI_State* ui_state = ctx->ui_state; \
        Config* cur_top = ui_state->cfg_bucket.c[name]; \
        if(IsNull(cur_top)) { \
            return default; \
        } \
        return *((type*)cur_top->ptr); \
    }
WidgetCfg
#undef X

#define X(name, type, default) \
    inline_function void name##_Push(type v) \
    { \
        Context* ctx = GlobalContextGet(); \
        UI_State* ui_state = ctx->ui_state; \
        Arena* arena = ui_state->arena_frame; \
        Config* cfg = PushStructZero(arena, Config); \
        type* v_ptr = PushStructZero(arena, type); \
        *v_ptr = v; \
        cfg->ptr=(void**)v_ptr; \
        cfg->type_enum=name; \
        Config** cur_top = &ui_state->cfg_bucket.c[name]; \
        StackPush(*cur_top, cfg); \
    }
WidgetCfg
#undef X

#define X(name, type, default) \
    inline_function void name##_Pop() \
    { \
        UI_State* ui_state = GlobalContextGet()->ui_state; \
        Arena* arena = ui_state->arena_frame; \
        Config** cur_top = &ui_state->cfg_bucket.c[name]; \
        ASSERT(*cur_top, "Should never pop a NULL value"); \
        StackPop(*cur_top); \
    }
WidgetCfg
#undef X

#define C_FontSize_Scoped(v) DeferScoped(C_FontSize_Push(v), C_FontSize_Pop())
#define C_Text_Scoped(v) DeferScoped(C_Text_Push(v), C_Text_Pop())
#define C_BackgroundColor_Scoped(v) DeferScoped(C_BackgroundColor_Push(v), C_BackgroundColor_Pop())
#define C_Softness_Scoped(v) DeferScoped(C_Softness_Push(v), C_Softness_Pop())
#define C_CornerRadius_Scoped(v) DeferScoped(C_CornerRadius_Push(v), C_CornerRadius_Pop())
#define C_BorderThickness_Scoped(v) DeferScoped(C_BorderThickness_Push(v), C_BorderThickness_Pop())
#define C_Margin_Scoped(v) DeferScoped(C_Margin_Push(v), C_Margin_Pop())
