#pragma once
#include <mimalloc.h>
#include <sanitizer/asan_interface.h>
#include <new>
#include <cstring>

struct alignas(64) MimallocProbeAligned
{
    char bytes[64];
};

int main(int argc, char** argv);
