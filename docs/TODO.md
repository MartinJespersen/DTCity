# Urgent changes
* vulkan descriptor layout specified twice some places (e.g. compute storage buffers descriptor)
* Reconsider the number of descriptor pools (whether 1 is enough) and the descriptor numbers
* In function RoadSegmentFromTwoRoadNodes: the normalize function leads to values being Nan. Handle this in a better way.

# Less urgent changes
* Improve the threading in the asset manager (e.g. too many mutexes are used at the moment)
* The implementation of draw functions and similar for compute is ugly
* Use vulkan push descriptor and buffer device address instead of descriptor sets
* buildings need to be rendered included in compute pass as well
* simplify render interface (a little too verbose at the moment)
* Reconsider async loading for none cesium objects.
* Remove the printf logging and find way to log the queue messages
* make descriptor_pool and functionallity part of asset_manager.
* Make TilesetRenderer into global variable
* Validation error for descriptors not used in shader when null texture is not filling in the wholes
* tile transform might need to be passed to shader as a uniform buffer
* validation errors at null texture destruction as it is still used by some cmd buffers.

# features
* Get NetAScore from HTTP API if possible.
  * read env variables from .env file
  * refactor to separate source file
  * move the netascore http call function to netascore layer
  * make the netascore results cacheable
  * Is web socket viable solution for this?
* Use the OSM data for showing data about buildings
* 3D geometry
  * Improve tile queue to avoid too many glitches
  * Get 3D geometry from host path
  * Create window to list 5-by-5km geometry with corresponding connection point.
* Simulation
  * Integrate with MATSim
* Make application work on MACOS
  * Make clang work as compiler

# Documentation
* Explain the use of NetAScore Environment variable.

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
