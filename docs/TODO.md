# Project TODO:
- How to show agent locations? Ability to change camera for each agent?
- Improve performance to be able to visualize large simulations.
- Visualize multiple scenarios (with dashboard for changing them)
  - visualization -> simulation communication for scenario change 
- Agent pr road visualization
  - heap map?

# Tessellation review findings
* Keep the current tile buffers active until replacement uploads have completed on the GPU.

# Less urgent changes
* Reconsider the number of descriptor pools (whether 1 is enough) and the descriptor numbers

# features
* Some roads are close to brown. Why? OOB?
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
