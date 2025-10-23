# Problems:
* Sometimes the wrong texture is used on objects, which is a problem with the loading use of textures in the asset manager.

# Things to do:

* Asset Store is currently not thread safe for eviction cases and memory needs to be managed properly.
  * When asset getting evicted we have to make sure no other threads are using the resource.
* Find way to flush logs so the memory does not build up forever
* Threads in asset store should manage have more than one available command buffer in the thread command pool.
* Linux support:
  * entrypoint caller currently implemented for windows should change to linux.
  * HTTP client implementation improvements on linux (move away from httplib)
  * hot reloading refactor to simplify the implementation for linux
  * Base library fixes
* Create method for logging


# On Linux
* For Khronos validation layer support (enabled if DEBUG_BUILD macro is defined) remember to install it:
  * sudo apt install vulkan-validationlayers (Ubuntu)
* Compiling (incl. linking) takes a very long time at the moment.
* hotreloading does not work at the moment, due to the entrypoint library not being unloaded using dlclose
  * This has been a problem defining TRACY_ENABLE in the past as the library overwrites dlclose. This is however not the current problem
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Mule Test Program
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 mule.cpp /DBUILD_CONSOLE_INTERFACE

# Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2

# TODO for the first draft
* Caching of OpenStreetMap data needs to be improved - &#9745;
* Bounding Box for OpenStreetMap should be a command line input - &#9745;
* Error handling:
  * OpenStreetMap information retry &#9745;
* Improve the HTTP library implementation
  * Probably consider using a cross platform library only instead of a mix
* Linux integration:
  * How to include linux libraries.
  * Build Script changes.
    * Should CMake be considered?
* Start documentation

## Nice to have:
* Improve the conversion between UTM and WGS84 system
* Make the optimized build version work
* Consider testing and how to do it
