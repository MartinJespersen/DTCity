# About

DTCity is an application for visualizing city data in 3D. 
The visualization is able to receive bounding box coordinates as input through the command line. Information about roads and buildings are fetched from OpenStreetMap. The information is either visualized in 3D or shown as text in a pop up window, when the object is hovered over.
A basic simulation of cars are also provided that show support for visualizing mesh data provided through a .gltf or .glb file.

# Usage
Binary executables for Linux and Windows along with dependencies are attached to each version of the application released on Github. 
The [usage-guide.pdf](docs/usage-guide.pdf) explains how to use the application after launch. 

# Build instructions

City uses independent layer unity translation units for incremental compilation.
See [incremental builds](docs/incremental_builds.md) for the layout, build-time
comparison mode, and benchmark script.

## Windows Prerequisites
* Install the Microsoft C/C++ Build Tools to install the MSVC compiler, CMake and vcpkg.
* Setup environment by running the script: ```vcvarsall.bat x64``` or ```use the x64 Native Tools Command Prompt``` that will automatically run the script when started.

## Linux Prerequisites (Ubuntu/Debian based)
A docker file is provided showing how to setup a ubuntu based build environment. 
The required packages can be installed with the following command: 
```bash
apt-get update && apt-get install -y \
    python3 \
    build-essential \
    git \
    curl \
    zip \
	unzip \
    tar \
    cmake \
    libvulkan1 \
    libgl1-mesa-dev xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules
```
To test the application for linux with WSL2 with docker, run the following commands inside WSL2. Make sure to enable WSL integration from the settings inside Docker Desktop with the distro you run the commands from.
```bash
docker compose up -d --build
docker compose exec city
apt update && apt install -y libvulkan1
./city
```

## 1. Create build directory

The CMakePresets.json file in the project root consists of 3 different build configurations for both x64 windows and x64 linux. 

The combinations are:
* debug-windows
* debug-linux
* release-windows
* release-linux
* profile-windows
* profile-linux

An example of setting up the build for the debug preset for windows is shown below:
- cmake --preset=debug-windows

For each of the 3 different build configurations, a build directory is created (build/<configuration_name>).
The different build configurations are: debug, release and profile.

To build for debug on either windows or linux, run the following commands:
- cmake --build build/debug

### Simulator debug build on Windows and Linux

Use the same preset commands on both systems:

```sh
cmake --preset debug-simulator
cmake --build --preset debug-simulator
```

This preset requires CMake 3.21 or newer and Ninja. It selects MSVC on Windows
and Clang on Linux, with AddressSanitizer enabled. On Windows, run it from the
x64 Developer Command Prompt. On Ubuntu, install Clang, LLD, and Ninja with
`sudo apt install clang lld ninja-build`, along with the Linux prerequisites above.
Outputs go to `simulator/build/Windows/debug-simulator/` or
`simulator/build/Linux/debug-simulator/`. The preset replaces `debug-simulator-linux`.

For Zed, keep the `Debug Simulator` task (F8) set to the two preset commands
above. Set `Run Simulator Debug` (F7) to
`cmake -P cmake/RunSimulator.cmake`, with the project root as its working
directory. The launcher selects the executable and working directory for the
current OS. Zed task files are ignored by Git and must be updated on each machine.

For profiling, use `profile-simulator` with the same configure and build commands.
It enables Tracy and optimization, with AddressSanitizer disabled. It replaces
`profile-simulator-linux` and uses the `profile-simulator` output folder on each OS.
Set Zed's `Simulator Profile Build` (Shift+F8) to these profile commands, and
`Run Simulator Profile` (Shift+F7) to
`cmake -Dsimulator_preset=profile-simulator -P cmake/RunSimulator.cmake`.

### Windows release with static dependencies

Build both DTCity and the simulator with static libraries and the static MSVC runtime:

```powershell
cmake --preset release-static-windows
cmake --build --preset release-static-windows
```

The executables are `build/win/release-static/city.exe` and
`build/win/release-static/Simulator.exe`. The preset disables debug instrumentation,
AddressSanitizer, and Tracy. It uses `x64-windows-static` instead of
`x64-windows-static-md`, which still depends on the shared MSVC runtime.

To build and collect both executables and their runtime files into a portable folder:

```powershell
cmake --build --preset package-release-static-windows
```

This creates `build/win/release-static/dist` containing both executables, `data/`
(models, textures, fonts, and compiled shaders), and `simulator/fonts/` and
`simulator/database/`. Downloaded caches and shader source files are excluded.
Resource lookup prefers files beside the executables, so the folder can be moved
and launched from another working directory. Development builds still fall back
to the source-tree resource paths.

Override the output folder with `-DDTCITY_PACKAGE_DIR=C:/path/to/dist` when configuring.
By default, the bundle includes `simulator/database/eskiltuna_test.sqlite` if it exists.
Set `-DDTCITY_PACKAGE_SIMULATOR_DATABASE=C:/path/to/playback.sqlite` to package another
database (renamed to `eskiltuna_test.sqlite`), or set the variable to an empty string to
omit the database and select one in the simulator UI. Packaging updates existing
files without deleting the output folder; use a fresh output folder for a clean release.

Windows and graphics driver components (including Vulkan/OpenGL) are still required
on the destination machine. Cesium ion credentials and network access are not bundled.

### Linux crash reports

Fatal signals print a stack report to `stderr` before the process terminates with
the original signal. Put `llvm-symbolizer` on `PATH` to include function names,
source files, and line numbers. Keep debug symbols in the executable (`Debug` or
`RelWithDebInfo`) for source locations. Without the tool, reports include module
paths and addresses that can be resolved later with GDB.

Symbolization uses local debug files and has a three-second time limit. Reporting
is best effort if the process state is damaged. Core dumps remain subject to the
system's core-dump configuration. Existing AddressSanitizer signal handlers are
preserved in sanitizer builds.

### C Macros
The following application specific macros are used to enable address sanitization, build tools and profiling:
* -DBUILD_DEBUG (Additional debug information e.g vulkan validation layer support)
* -DASAN_ENABLED=ON (enable address sanitizer support)
* -DTRACY_PROFILE_ENABLE (Enable tracy profiling)
* -DSHADER_DEBUG=ON (Shader debug information; requires BUILD_DEBUG=ON)

`VK_KHR_shader_relaxed_extended_instruction` is required only when both
`BUILD_DEBUG` and `SHADER_DEBUG` are enabled. Compiled shaders are kept in each
build directory under `shaders/bin`, so debug and release builds cannot overwrite
each other's shaders. Packaging copies the selected build's shaders into `data/shaders/bin`.

CMake presets define these macros based on what type of build configuration is used - debug, release or profile. These defaults can be changed e.g. you might want to enable address sanitization in a profile build.

### Simulator event streaming

Prepare MATSim imports with this folder layout:

```text
simulator/input/event_files/<scenario>.xml  # one or more events files
simulator/input/network_file/network.xml   # shared network
simulator/input/map_file/<map file>         # one map file
```

Run `python simulator/scripts/import_playback_sqlite.py` from the repository root.
The script writes one database per events file to `simulator/output/`, replacing
only the final `.xml` extension with `.sqlite`. For example, `city.day 1.xml`
becomes `city.day 1.sqlite`. Defaults use paths beside the script, so running from
another working directory produces the same outputs. The network is loaded once;
event limits apply separately to each events file. Existing matching databases
are replaced on each run.

The network and map files must exist before importing. Coordinates come from the
network XML; the map file is checked for presence and is not inserted into SQLite.
Projected network coordinates require `pyproj` (`python -m pip install pyproj`).
Use `--network-crs` to override the network CRS when needed. Use `--events` with
one or more XML files or folders, `--network` and `--map` to select shared input
files, and `--output` to select an output folder. These options also support files
placed directly in `simulator/input/`.

The simulator discovers `.sqlite`, `.sqlite3`, and `.db` files in `simulator/database/`.
Metadata lists their filenames without the final extension as scenario names.
The client selects a name, and the query worker reads `agent_events` from that file.
Metadata retains one shared time range from the database loaded in the simulator UI.
The client sends `ServerUpdate` with the scenario `name`, current
`playback`, a fetch `period` in seconds, and a `request_id`. Requests are sent on
seeks and scenario changes, and every half-period (default: 5 seconds).

Replies use `Stream`, echo `request_id`, and contain a chronological `stream`.
Each event contains `id`, `time`, `event_type`, `node_from_id`, `node_to_id`,
`lon_from`, `lat_from`, `lon_to`, and `lat_to`. A reply includes the latest event
strictly before playback for each active agent, plus all events in
`[playback, playback + period)`. The client buffers future events and only applies
those with `time < playback`.

Event type 4 is arrival. Completed agents are excluded from the baseline;
arrivals in the requested window retire existing client agents without allocating
new slots. Full replies also remove agents absent from the reconstructed state,
so missed arrivals cannot retain historical agents. Later trips reuse pool slots
with fresh handle generations. See [time_sync.md](time_sync.md) for protocol details.

### Mimalloc and AddressSanitizer

The application, Simulator, and base-layer tests replace global C++ `new`/`delete`
with mimalloc, including ASan builds. All use mimalloc 3.5.3 from the
[`vcpkg-ports/mimalloc`](vcpkg-ports/mimalloc/README.md) overlay. Set `VCPKG_ROOT`
before configuring. Simulator-only presets use the smaller manifest in
`simulator/`; other presets use the root manifest. Detailed allocator statistics
are disabled.

With `ASAN_ENABLED=ON`, use `x64-windows-static-md-asan` for MSVC or
`x64-linux-asan` for Clang on Linux. The host toolchains and the `debug-linux`
preset select these variants. CMake rejects mismatched allocator and application
ASan settings. Use a fresh build directory when changing the toolchain or triplet.

The ASan dependency includes a small patch that restores poisoning of unused bytes
at the end of rounded allocation blocks. Without it, the overflow probe misses a
one-byte write past a 64-byte allocation. The overlay's `asan.patch` applies
this correction and supplies MSVC-compatible ASan build flags. CRT `malloc`/`free` are not overridden.
This integration does not provide all the diagnostics or quarantine behavior of
ASan's own heap allocator; mimalloc reports poisoned-memory accesses through ASan.

With `BUILD_TESTS=ON`, build `city_tests`, `arena_debug_tests`, `utm_tests`, and
`mimalloc_counter_probe`. The counter probe checks sustained allocation with
allocator statistics disabled.
ASan configurations also provide `mimalloc_asan_probe`. Run
`ctest --test-dir <build-directory>/tests -R "^(city_tests|arena_debug|utm_tests|mimalloc_counter|mimalloc_asan_.*)$" --output-on-failure`.
The ASan probes
check allocation ownership and poisoned boundaries, and run deliberate overflow
and use-after-free accesses in separate processes, requiring an ASan error report.


With Vulkan headers, a loader, and `glslangValidator` installed, `BUILD_TESTS=ON`
also provides `model_lod_compute_test`. Build it and run
`ctest --test-dir <build-directory>/tests -R model_lod_compute --output-on-failure`.
It runs the production agent LOD compute shader on a hardware or software Vulkan
device and checks indirect commands, instance offsets, dispatch boundaries, and
buffer reuse, plus transfer-write visibility through the production upload barrier. It skips when no suitable compute device is available. Vulkan
validation, including synchronization validation, is enabled when installed.
The shader layout and graphics interface checks can be run separately with
`python3 tests/render/test_shader_layouts.py --glslang <glslangValidator> --glm-include <include-directory>`.

With GLFW available, `swapchain_sync_test` checks 128 acquire/render/present cycles
with synchronization validation. It skips if the display, validation layer, or a
suitable Vulkan device is unavailable. On Linux, a headless software run is:
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a <build-directory>/tests/swapchain_sync_test`.
The optional `--legacy-scopes` argument reproduces the old presentation hazards;
`model_lod_compute_test <shader.spv> --legacy-upload-sync` omits the upload barrier
to check that synchronization validation detects the transfer hazard.
