# DTCity mimalloc port

This overlay builds mimalloc 3.5.3 through vcpkg. It starts from the official
vcpkg port and keeps its static/shared target setup and pkg-config correction.
The port version must increase when this recipe or its patches change after release.

`asan.patch` restores the red zone after allocation initialization and adapts
upstream's ASan build flags for MSVC. ASan tracking is enabled only by the
`x64-windows-static-md-asan` or `x64-linux-asan` triplet. The package installs
`dtcity-mimalloc-config.cmake` so consumers can reject a mismatched ASan variant
at configure time. Detailed allocator statistics are disabled.

`page-counter.patch` keeps page counters reset with `MI_STATS=OFF`. Without this
correction, repeated allocations overflow the 16-bit allocation counter and
trigger a debug assertion. The `mimalloc_counter` test keeps blocks live while
recycling allocations past that limit.

The root manifest supplies the city and tests. Simulator presets use the smaller
manifest in `simulator/`, which installs only mimalloc. Set `VCPKG_ROOT` before
configuring. Use a fresh build directory when changing the toolchain or triplet.

After updating mimalloc, rebase the patches and run all three ASan probes on
Windows and Linux. A passing compile alone does not verify red zones.
