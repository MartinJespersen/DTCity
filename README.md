# Build instructions

## Windows Prerequisites
* Install the Microsoft C/C++ Build Tools to install the MSVC compiler, CMake and vcpkg.
* Setup environment by running the script: ```vcvarsall.bat x64``` or ```use the x64 Native Tools Command Prompt``` that will automatically run the script when started.

## 1. Create build directory

The different cmake preset and its configuration can be found in CMakePreset.json file in the project root. It currently consist of 3 different presets or build configurations. These are Debug, Release and profile.

An example of setting up the build for the Debug preset is shown below.
- cmake --preset=Debug

To build simply run:
- cmake --build build

### C Macros
The following application specific macros are used to enable address sanitization, build tools and profiling:
* -DBUILD_DEBUG (Additional debug information e.g vulkan validation layer support)
* -DASAN_ENABLE=ON (enable address sanitizer support)
* -DTRACY_PROFILE_ENABLE (Enable tracy profiling)

# TODO along the way
* Add linting and static analysis checks
* Change the naming convention for functions from PascalCase to snake_case
* Cleanup entrypoint main loop

## TODO: Done
* Caching of OpenStreetMap data needs to be improved
* Bounding Box for OpenStreetMap should be a command line input
* Error handling:
  * OpenStreetMap information retry
* Move OSM data structure to its own layer
* Show the current road or building name in imgui window
  * Create a hashmap for Ways for quick lookup of index.
  * This requires a centralized place for storing ways and nodes.
* Linux integration:
  * Make ASAN work
  * Have warnings
* error: refetch json from osm if error happens in json parsing.
  * terminate called after throwing an instance of 'simdjson::simdjson_error'
    what():  INCORRECT_TYPE: The JSON element does not have the requested type.
* Imgui -> third_party dir
* Make the optimized build work and make possible to profile
* CMake
  * compile shaders
* Create github action to build system for linux and windows
* Linux glslangValidator executable should be added to tools directory
* Avoid validation layers in release builds
* Documentation
  * Explain the different layers such as render, ui, etc.
  * Create diagram of interaction between layers
  * Explain how to navigate in the application
  * Explain how to run the application with different bounding boxes.

# Future Improvements:
* Layers should compile as seperate units
* Error handling improvements - error handling should not be ExitWithError everywhere
* Improve the conversion between UTM and WGS84 system
* Consider testing and how to do it
* Improve the HTTP library implementation
  * Probably consider using a cross platform library only instead of a mix
* Asset Store is currently not thread safe for eviction cases and memory needs to be managed properly.
  * When asset getting evicted we have to make sure no other threads are using the resource.
* Threads in asset store should manage have more than one available command buffer in the thread command pool.
* Linux support:
  * entrypoint caller currently implemented for windows should change to linux.
  * HTTP client implementation improvements on linux (move away from httplib)
  * hot reloading refactor to simplify the implementation for linux
  * Base library fixes
* Make executable stand alone:
  * What to do about shaders
    * compiled directly into executable?
  * What to do about assets and textures that are not part of executable
* Add performance tests in developer workflow and in Github Actions

# Linux Info
* Compiling (incl. linking) takes a very long time at the moment.
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Helpers
## Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2


## Further steps could be
* Add functionality tests for important features of the software.
* Create a work plan to move the current codebase to the desired design state.
* Add unit tests for all new code contributions while updating the functionality tests
