#pragma once

// tread context
#ifdef __linux__
#define per_thread __thread
#elif defined(_MSC_VER)
#define per_thread __declspec(thread)
#else
#error thread context not implemented for os
#endif

// Widget Configuration --------------------------------

#define X(name, type) \
    inline_function type name##_Get();
WidgetCfg
#undef X

#define X(name, type) \
    inline_function void name##_Push(type v);
WidgetCfg
#undef X

#define X(name, type) \
    inline_function void name##_Pop();
WidgetCfg
#undef X


root_function void
ThreadContextInit();

root_function void
ThreadContextExit();

no_name_mangle void
ThreadCxtSet(ThreadCtx* ctx);

root_function ThreadCtx* 
ThreadCtxGet();

// globals context
no_name_mangle void GlobalContextSet(Context* ctx);
inline_function Context* GlobalContextGet();

