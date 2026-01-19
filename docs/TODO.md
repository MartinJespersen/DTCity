# Bugs
* App should work without netascore geo.json file
* After deleting everything in the deletion queue, delete the asset item queue and give warning if it is not empty.
* imgui assertion happens during array indexing operation (Assertion failed: i >= 0 && i < Size)
* Direction or previous node is needed for random neighbor algorithm to avoid doing a u-turn when it is possible to drive straight.


# Changes along the way
* Make the asset manager thread safe - accessing texture and buffer lists are not thread safe at the moment
* Add linting and static analysis checks
* Change the naming convention for functions from PascalCase to snake_case
* Cleanup entrypoint main loop

# Future Improvements:
* Improve BufferInfo creation and especially the render interface functions.
* Descriptor set layouts are badly handled at the moment and the how it is allocated, used and destroyed should be improved.
* make vma allocator part of the asset manager (seperate asset manager from other parts of application)
* Layers should compile as seperate units
* Error handling improvements - error handling should not be ExitWithError everywhere
* Improve the conversion between UTM and WGS84 system
* Consider testing and how to do it
* Improve the HTTP library implementation
  * Probably consider using a cross platform library only instead of a mix
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
