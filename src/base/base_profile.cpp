static U64
cpu_timer_freq_estimate()
{
    U64 ms_to_wait = 100;
    U64 os_freq = OS_SystemTimerFreqGet();

    U64 cpu_start = os_cpu_timer_read();
    U64 os_start = OS_SystemTimerRead();
    U64 os_end = 0;
    U64 os_elapsed = 0;
    U64 os_wait_time = os_freq * ms_to_wait / 1000;
    while (os_elapsed < os_wait_time)
    {
        os_end = OS_SystemTimerRead();
        os_elapsed = os_end - os_start;
    }

    U64 cpu_end = os_cpu_timer_read();
    U64 cpu_elapsed = cpu_end - cpu_start;

    U64 cpu_freq = 0;
    if (os_elapsed)
    {
        cpu_freq = (os_freq * cpu_elapsed) / os_elapsed;
    }

    return cpu_freq;
}

static F64
us_from_cpu_cycles(U64 cycles)
{
    U64 cpu_freq = cpu_timer_freq_estimate();
    U64 micro = 1'000'000;
    F64 time_elapsed_us = (F64)(cycles * micro) / (F64)cpu_freq;
    return time_elapsed_us;
}
