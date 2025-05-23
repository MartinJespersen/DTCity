#pragma once

// tread context
#ifdef __linux__
#define per_thread __thread
#elif defined(_MSC_VER)
#define per_thread __declspec(thread)
#else
#error thread context not implemented for os
#endif

internal void
ThreadContextInit();

internal void
ThreadContextExit();

C_LINKAGE void
ThreadCxtSet(ThreadCtx* ctx);

internal ThreadCtx*
ThreadCtxGet();

// globals context
C_LINKAGE void
GlobalContextSet(Context* ctx);
internal Context*
GlobalContextGet();
