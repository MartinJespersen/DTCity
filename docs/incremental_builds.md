# Incremental city builds

The default city build compiles twelve independent translation units in
`src/build_units/`. Each unit includes its layer's implementation files, keeping
unity compilation within the layer. Third-party implementation files remain
separate. An implementation edit rebuilds its owning object and relinks `city`.

| Translation unit | Implementation ownership |
| --- | --- |
| `base.cpp` | Base, OS backend, debug logging |
| `utility.cpp` | Coordinate conversion |
| `async.cpp` | Thread pool, HTTP, WebSockets |
| `json.cpp` | JSON library wrappers |
| `gltf.cpp` | glTF loading |
| `render.cpp` | Rendering and Vulkan |
| `draw.cpp` | Draw commands |
| `misc.cpp` | Geometry, input, camera |
| `osm.cpp` | OpenStreetMap |
| `city.cpp` | City data, simulation interface, tile loading |
| `cesium.cpp` | Cesium tile integration |
| `app.cpp` | Entrypoint and main loop |

Each layer unit has its own required header list. `core_inc.hpp` supplies common
base types, memory services, thread context, OS declarations, and debug macros.
Optional base utilities are included only by their consumers. `includes.hpp` is
used only by the monolithic unit. All units share the existing third-party PCH,
`pch.hpp`.
Project headers and implementation files stay outside that PCH, so editing them
does not invalidate the expensive third-party precompilation.

Mutable allocator defaults, async thread-local state, application context,
OS backend state, Vulkan entry points, and debug logging each have one
implementation owner. Cross-layer functions have external linkage. Template
definitions live in separate `*_templates.hpp` files and are included after their
layer declarations. They remain visible to callers for instantiation; the template
files are not separate CMake translation units. `base_templates.hpp` collects the
base definitions, while `core_inc.hpp` includes only the common allocator and
container definitions. Backend-specific rendering templates live in
`vulkan_if_templates.hpp`. Private Vulkan asset-manager templates remain inside
the rendering unit. Worker queue and timer
heap headers are included by `async_inc.cpp`, rather than by async API consumers.
`simulation.hpp` forward-declares the transport client; its complete header is
included by `city_inc.cpp`. Pointer-only dependencies use forward declarations.

## Building

Existing presets use the layer build automatically. Ninja can compile independent
objects concurrently:

```powershell
cmake --preset debug-windows
cmake --build build/win/debug --target city --parallel
```

Add implementation files to the owning `*_inc.cpp`, or to its wrapper in
`src/build_units/`. Register a new layer wrapper in `CITY_LAYER_SOURCES` in
`CMakeLists.txt`. Do not add included implementation files directly as additional
CMake sources, which would compile their definitions twice. `clang-tidy-check`
checks the selected translation units.

The equivalent monolithic build is available for comparisons:

```powershell
cmake --preset debug-windows -B build/win/debug-monolithic -DCITY_MONOLITHIC_BUILD=ON -DVCPKG_INSTALLED_DIR="$PWD/build/win/debug/vcpkg_installed"
cmake --build build/win/debug-monolithic --target city --parallel
```

Use identical compiler, linker, dependency, debug, sanitizer, and profiler settings
when comparing the modes. `CITY_MONOLITHIC_BUILD` is off by default. CMake's own
automatic unity merging is disabled for `city` to preserve these explicit groups.

## Measuring

```powershell
./scripts/benchmark_incremental.ps1 -build_directory build/win/debug
./scripts/benchmark_incremental.ps1 -build_directory build/win/debug-monolithic
```

The script warms the build, touches five representative implementation files,
and reports median wall time over three builds per file. It verifies that each
edit compiles exactly one object and restores the original source timestamps,
without changing source contents. Raw results are saved in
`incremental-benchmark.csv` in the build directory. Run comparisons sequentially
with other builds stopped. Wall time includes shader dependency checks, compiler,
linker, and dependency-copy commands. Compiler-only durations are available in
Ninja's `.ninja_log`.

Header edits rebuild only the units that include them. For example, `city.hpp`
affects city and app; `gltfw.hpp` affects glTF and city; `async_heap.hpp` affects
async. Shared base headers still affect many units. Template edits rebuild their
consumers. PCH edits rebuild all
application objects. Clean builds trade repeated project-header parsing for
parallel compilation; the best grouping can vary by machine. Measure clean and
incremental builds separately before splitting a layer further. Linking remains
a substantial part of the measured incremental wall time.

### Measured results

The following measurements were collected in `fleet-owl` before these changes
were ported to `main`; they are reference results for that version of the code.

Windows MSVC 19.51 debug builds with LLVM's linker, reused dependencies/PCH, and
three repetitions per edit produced these median times. Comparisons ran
sequentially on a machine with sixteen logical processors. The monolithic column
uses `CITY_MONOLITHIC_BUILD=ON` with the same code and build settings.

| Edited implementation | Monolithic wall time | Layer wall time | Compiler time: monolithic ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ layer |
| --- | ---: | ---: | ---: |
| Utility / UTM | 5.51 s | 3.65 s | 2.33 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 0.49 s |
| Async / thread pool | 5.53 s | 3.62 s | 2.28 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 0.51 s |
| Vulkan pipelines | 5.60 s | 3.91 s | 2.42 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 0.75 s |
| City | 5.74 s | 4.07 s | 2.32 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 0.88 s |
| Cesium tiles | 5.62 s | 4.69 s | 2.37 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 1.49 s |

These edits reduced wall time by 17ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“35% and compiler time by 37ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“79%. Every sampled
edit compiled exactly one object and reused the PCH and third-party objects.

Rebuilding all application objects with eight compiler jobs took a median of
5.03 s in layer mode versus 6.00 s in monolithic mode, including linking. The
layer source list starts larger units first; the earlier ordering took 6.39 s.
This measures application recompilation with an existing PCH and third-party
objects, rather than a complete clean build or dependency installation. Results
will vary with hardware, linker, compiler, and build configuration.

## Validation

After porting to `main`, Windows MSVC debug, release, and monolithic application
builds pass. The full Linux GCC release application build also passes. Both test
executables pass on Windows and Linux (76 test cases). Tests include a separate
translation unit that checks shared thread context, allocator defaults, and
template-backed allocation across the object boundary.

One incremental build per implementation in the table above confirmed that each
edit recompiles exactly one application object and reuses the PCH and third-party
objects. These checks validate the dependency boundaries; the reference timings
above were not remeasured for this branch.

The header include lists were reduced without changing function bodies. Candidate
include removals were checked with the MSVC compiler; each owning unit retains its
interface header to check declaration and definition consistency. Windows debug,
release, monolithic, and Linux release application builds pass with these lists.
