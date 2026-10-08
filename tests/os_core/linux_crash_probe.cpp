#include "linux_crash_probe.hpp"
#include "base/base_inc.cpp"

int
App(int argc, char** argv)
{
    // Test children must terminate by signal without creating core files.
    rlimit core_limit = {};
    setrlimit(RLIMIT_CORE, &core_limit);
    if (argc > 2)
    {
        int symbolizer_mode = strcmp(argv[1], "--relative-address");
        if (symbolizer_mode == 0)
        {
            // Used as a deliberately stalled symbolizer by the timeout test.
            poll(0, 0, -1);
            return 4;
        }
    }
    if (argc != 2)
        return 2;

    int worker_mode = strcmp(argv[1], "worker");
    int abort_mode = strcmp(argv[1], "abort");
    int segv_mode = strcmp(argv[1], "segv");
    int overflow_mode = strcmp(argv[1], "overflow");
    int safe_call_mode = strcmp(argv[1], "safe_call");
    if (worker_mode == 0)
    {
        OS_Handle worker = OS_ThreadLaunch(_os_lnx_crash_probe_fail, 0, 0);
        OS_ThreadJoin(worker, max_U64);
    }
    else if (abort_mode == 0)
        abort();
    else if (segv_mode == 0)
    {
        volatile U8* invalid_address = (volatile U8*)1;
        *invalid_address = 1;
    }
    else if (safe_call_mode == 0)
        os_safe_call(_os_lnx_crash_probe_fail, 0, 0);
    else if (overflow_mode == 0)
        _os_lnx_crash_probe_overflow(max_U32);
    else
        _os_lnx_crash_probe_fail(0);
    return 3;
}

lib_internal void
_os_lnx_crash_probe_fail(void* ptr)
{
    (void)ptr;
    AssertAlways(0 && "deliberate crash reporter test");
}

lib_internal void
_os_lnx_crash_probe_overflow(U32 depth)
{
    volatile U8 stack_bytes[8192] = {};
    stack_bytes[0] = (U8)depth;
    if (depth > 0)
        _os_lnx_crash_probe_overflow(depth - 1);
    // Keep a stack frame at each depth, including in optimized test builds.
    stack_bytes[1] = stack_bytes[0];
}
