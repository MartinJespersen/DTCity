# Important changes before committing:
- make sure the vulkan function are not used in city layer

# Regressions
* [Medium] Include GPU-pending tile replacements in the global reprocessing limit. Counting only active CPU task nodes allows staging buffers and Vulkan submissions to accumulate after the CPU tasks finish.
* [Medium] Increment the tile mesh processor generation only when mesh classification inputs change. Changing or re-enabling a shader overlay option should not re-tessellate every tile.

# Project TODO:
- Improve performance to be able to visualize large simulations.
  - multithreaded addition
- Ability to change camera for each agent?
- Visualize multiple scenarios (with dashboard for changing them)
  - Visualization -> simulation communication for scenario change 
- Agent pr road visualization
  - heap map?

# Tessellation review findings
* Keep the current tile buffers active until replacement uploads have completed on the GPU.


# features
* chunk_list should not allow chunk items with count larger than capacity
* Show a clock in the application allowing 
* agents cannot be deleted due to caching and cesium async height calculation
* delete draw flush and related code
* Osm data visualizer should not be affected by netascore not showing
* Use the OSM data for showing data about buildings
  * buildings need to be rendered included in compute pass as well
* simplify render interface (a little too verbose at the moment)
* Logging should be improved to not always print to console 
  * Create memory viewer
* For Cpp Allocator
  * Should work on arrays and initializer lists as well
  * std::construct_at and std::destroy_at could be used instead of what is done at the moment.
* 3D geometry
  <!--* include LOD2 geometry-->
  * Get 3D geometry from host path
  * Create window to list 5-by-5km geometry with corresponding connection point.
* Make application work on arm arhitecture 
  * Make clang work as compiler
  * Make app work on MACOS
* Create visualizer for arena allocations
* Make shader bin directory be build specific (so that recompilation is triggered if other build type is used)
* Asset management changes
  * Improve the threading in the asset manager (e.g. too many mutexes are used at the moment)
  * Use a list of fences for draw and compute calls that waits for asynchrounously loaded assets

# Debug Log Suggestions
* arena: alloc, push, pop and releases
* HTTP debug log
# Documentation
* Explain the use of NetAScore Environment variable.

# Future Improvements:
* Create a draw layer 
* Improve BufferInfo creation and especially the render interface functions.
* Descriptor set layouts are badly handled at the moment and the how it is allocated, used and destroyed should be improved.
* Layers should compile as seperate units
* Error handling improvements - error handling should not be ExitWithError everywhere
* Consider testing and how to do it
* Improve the HTTP library implementation
  * Probably consider using a cross platform library only instead of a mix
* Threads in asset store should manage have more than one available command buffer in the thread command pool.
* Linux support:
  * HTTP client implementation improvements on linux (move away from httplib)
* Make executable stand alone:
  * What to do about shaders
    * compiled directly into executable?
  * What to do about assets and textures that are not part of executable
* Add performance tests in developer workflow and in Github Actions

# Tools or features for debugging
* validation layers should show the source location of layer
* Vulkan debugging: https://www.youtube.com/watch?v=UeWXr0i7eBY
* Clang ThreadSanitizer (for detecting race conditions)
* WhiteBox (by Andrew Reece) 
* /fsanitize=fuzzer option in MSVC 
* GL_EXT_debug_printf for making debugging shaders easier
* VkPhysicalDeviceFeatures::robustBufferAccess debugging shaders(OOB writes ignored and loads are zeroed)
  * Extension2 is available for descriptor sets (descriptor_indexing OOB)
* VkDebugUtilsMessengerEXT for debugging Vulkan API calls
* GPU-Assisted Validation (GPU-AV)
  * helps with Buffer Device Addresses (currently not used)
* VK_EXT_debug_utils
  * label buffer and images 
  * label regions in command buffer
* GPU printf 
  * SPV_KHR_non_semantic_info (16:06 in video above)
* What to do about VK_DEVICE_LOST_LOST (video at 18:00)
* Other tools (video 18:30)

# Steps for new simulator integration:
- Make visualization run smooth with a large number of agents: 
  - LOD changes 
- Create the general API for the simulator.
- Create a simple flow of: get scenarios -> set scenarios -> start scenario -> stop scenario -> disconnect
- Make multiple scenarios possible
