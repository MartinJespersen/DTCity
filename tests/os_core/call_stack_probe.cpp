#include "call_stack_probe.hpp"
#include "base/base_inc.cpp"

int
App(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    os_call_stack_print();
    // This diagnostic must return without terminating the caller.
    return 0;
}
