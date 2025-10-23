# TODO for the first draft
* Show the current road or building name in imgui window
  * Create a hashmap for Ways for quick lookup of index.
  * This requires a centralized place for storing ways and nodes.
* Improve the HTTP library implementation
  * Probably consider using a cross platform library only instead of a mix
* Linux integration:
  * How to include linux libraries.
  * Build Script changes.
    * Should CMake be considered?
* Documentation
  * Explain the different layers such as render, ui, etc.
* Caching of OpenStreetMap data needs to be improved - &#9745;
* Bounding Box for OpenStreetMap should be a command line input - &#9745;
* Error handling:
  * OpenStreetMap information retry &#9745;

## Nice to have:
* Improve the conversion between UTM and WGS84 system
* Make the optimized build version work
* Consider testing and how to do it

# Future Improvements:
* Asset Store is currently not thread safe for eviction cases and memory needs to be managed properly.
  * When asset getting evicted we have to make sure no other threads are using the resource.
* Threads in asset store should manage have more than one available command buffer in the thread command pool.
* Linux support:
  * entrypoint caller currently implemented for windows should change to linux.
  * HTTP client implementation improvements on linux (move away from httplib)
  * hot reloading refactor to simplify the implementation for linux
  * Base library fixes

# Linux Info
* For Khronos validation layer support (enabled if DEBUG_BUILD macro is defined) remember to install it:
  * sudo apt install vulkan-validationlayers (Ubuntu)
* Compiling (incl. linking) takes a very long time at the moment.
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Mule Test Program
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 mule.cpp /DBUILD_CONSOLE_INTERFACE

# Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2
