# TODO for the first draft

* Imgui -> third_party dir
* Cleanup entrypoint main loop
* Change the naming convention for functions from PascalCase to snake_case
* Make the optimized build work and make possible to profile
* CMake
  * compile shaders
* Make executable stand alone:
  * What to do about shaders
    * compiled directly into executable?
  * What to do about assets and textures that are not part of executable
* Documentation
  * Explain the different layers such as render, ui, etc.
  * Create diagram of interaction between layers

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

# Future Improvements:
* Layers should compile as seperate units
* Error handling improvements - error handling should not be ExitWithError everywhere
* Improve the conversion between UTM and WGS84 system
* Make the optimized build version work
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

# Linux Info
* For Khronos validation layer support (enabled if DEBUG_BUILD macro is defined) remember to install it
  * The validation layer are part of the lunarg sdk that can be down loaded from the site: https://vulkan.lunarg.com/doc/sdk/1.4.328.1/linux/getting_started.html
* Compiling (incl. linking) takes a very long time at the moment.
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Mule Test Program
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 mule.cpp /DBUILD_CONSOLE_INTERFACE

# Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2

# Long Term Features
* Usage of Aarhus Municipality mesh data.

# Prasad Suggestions
Thanks for the URL. This is the project I completed in 2020. The only lesson to take away from that repo is to have cppcheck and googletest. If I were to start now, the following order of things could make sense.

* Set project structure. Your favourite nginx could be a good example. Here is another example project structure.
* Documentation for developers and users. It could either be a single one or separate for developers and users.
* Use a build system (vcpkg / conan / CMake)
* Basic documentation (three to ten pages) explaining the use of the software. It could be a markdown file in docs/README.md.
* Github actions for running through all commands to build software. See an example. Let the action upload build binaries of different platforms on the workflow page.
* Make the first release

If you could complete these steps before our meeting on 13-Nov-2025, that would be a good milestone.

## Further steps could be
* Add linting and static analysis checks
* Add functionality tests for important features of the software.
* Document the desired system architecture and design. This could either be done using mermaidjs or plantuml. Here is an example.
* Create a work plan to move the current codebase to the desired design state.
* Add performance tests in developer workflow and in Github Actions
* Add unit tests for all new code contributions while updating the functionality tests

# CMake build instructions
## 1. Create build directory
Debug:
cmake -B build -DCMAKE_BUILD_TYPE=Debug
Release:
cmake -B build -DCMAKE_BUILD_TYPE=Release
## 2. Build Project
cmake --build build
