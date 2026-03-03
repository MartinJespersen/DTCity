# Urgent changes
* vulkan descriptor layout specified twice some places (e.g. compute storage buffers descriptor)
* Reconsider the number of descriptor pools (whether 1 is enough) and the descriptor numbers
* In function RoadSegmentFromTwoRoadNodes: the normalize function leads to values being Nan. Handle this in a better way.
* Check barriers for command buffer recordings

# Less urgent changes
* Use vulkan push descriptor and buffer device address instead of descriptor sets
* buildings need to be rendered included in compute pass as well
* simplify render interface (a little too verbose at the moment)
* Reconsider async loading for none cesium objects.
* Make the vcpkg overlay port work for cesium instead of the using the local repo.
* look at the descriptor index alloc and free functions again.
* Remove the printf logging and find way to log the queue messages
* Find out how to make the coordinate system and 3D geometry match.
* make descriptor_pool and functionallity part of asset_manager.
* Make TilesetRenderer into global variable
* Validation error for descriptors not used in shader when null texture is not filling in the wholes
* tile transform might need to be passed to shader as a uniform buffer
* validation errors at null texture destruction as it is still used by some cmd buffers.

# Bugs
* imgui assertion happens during array indexing operation (Assertion failed: i >= 0 && i < Size)
* Direction or previous node is needed for random neighbor algorithm to avoid doing a u-turn when it is possible to drive straight.

# Changes along the way
* Make the asset manager thread safe - accessing texture and buffer lists are not thread safe at the moment
* Add linting and static analysis checks
* Change the naming convention for functions from PascalCase to snake_case
* Cleanup entrypoint main loop

# Tools or features for debugging
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

# Future Improvements:
* Create a draw layer 
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
