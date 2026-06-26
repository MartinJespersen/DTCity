# Urgent changes
* All vulkan load function should take in thread_ctx 
* Some roads are not covered in NetAScore at certain LOD's.

# Less urgent changes
* Reconsider the number of descriptor pools (whether 1 is enough) and the descriptor numbers
* In function RoadSegmentFromTwoRoadNodes: the normalize function leads to values being Nan. Handle this in a better way.
* Improve the threading in the asset manager (e.g. too many mutexes are used at the moment)
* Remove the printf logging and find way to log the queue messages
* make descriptor_pool and functionallity part of asset_manager.
* tile transform might need to be passed to shader as a uniform buffer

# features
* delete draw flush and related code
* record asset lifetimes similar to how debug events are done at the moment to be sure everything related to asset lifetimes are handled on the same thread.
* Use a list of fences for draw and compute calls that waits for asynchrounously loaded assets
* Osm data visualizer should not be affected by netascore not showing
* It should be possible to switch between cities in the editor
* Use the OSM data for showing data about buildings
  * buildings need to be rendered included in compute pass as well
* simplify render interface (a little too verbose at the moment)
* Logging should be improved to not always print to console 
  * Create memory viewer
* 3D geometry
  <!--* include LOD2 geometry-->
  * Improve tile queue to avoid too many glitches
  * Get 3D geometry from host path
  * Create window to list 5-by-5km geometry with corresponding connection point.
* Make application work on arm arhitecture 
  * Make clang work as compiler
  * Make app work on MACOS
* Memory handling with shared pointer might not be the way to go for http 

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
