* Does road intersect with triangle at all?
  * Test using SAH
* If no, triangle should become part of Terrain
* If yes, and whole triangle is inside road, then make triangle part of road
* If yes, but some part is Terrain and other is Road, then triangulate
  * triangulation algorithm steps
    * Find vertices from road inside 2D triangle
    * Find vertices from road created by the intersection of the road polygon with the triangle (clipping of triangle)
    * Generate triangle, but
      * Use road edges inside triangle as constraint, meaning generated triangle edges cannot intersect with road edges
* Classify triangulated triangles into Road or Terrain
* Determine vertex heights of generated vertices
* Change UV coordinates by

# Tessellation review findings
* [Critical] Fix CPU BVH traversal in `tessellate_roads_overlapping`. Pop the current node before pushing its children; the deferred pop currently removes a child and leaves the parent on the stack, causing repeated traversal or an infinite loop.
* [High] Tessellate every SAT-confirmed overlap case, including when all face vertices are outside the road, all face vertices are inside the road, or a road crosses a coarse face without containing any face vertex.
* [High] Collect all road constraints for a face and run constrained Delaunay triangulation once. Triangulating the complete face independently for every overlapping road creates duplicate coplanar geometry.
* [High] Replace the unsynchronized global `g_bvh_result` pointer with ownership and synchronization that are safe for road-build and tile worker threads. Clear or invalidate access before releasing the road arena.
* [High] Reprocess or invalidate tiles loaded before the road BVH becomes available so CPU tessellation does not depend on asynchronous tile-loading order.
* [High] Make the compute fallback assign the road-segment index and object ID required by `tile.tese`, or remove the fallback after CPU tessellation handles every tile. It currently changes only `overlay_option`, so untessellated triangles cannot enter the road-rendering path.
* [High] Restore safe road-segment coalescing, or replace it with another join-generation method. Generating adjacent segments independently can leave gaps and overlapping geometry at bends.
* [Medium] Reject or handle clipped polygons with fewer than three non-collinear vertices before constructing constrained edges and passing them to CDT.
* [Medium] Normalize or account for the projected face winding before CCW Sutherland-Hodgman clipping, and verify generated and source triangle winding with backface culling enabled.
* [Medium] Handle zero and near-zero denominators in `intersect_segment_plane` before division so parallel or nearly parallel segments cannot generate infinite or NaN intersection vertices.
* [Low] Measure the loading and CPU-tessellation cost of reducing Cesium's `maximumScreenSpaceError` from 16 to 8 before keeping the denser tile-selection setting.

# Less urgent changes
* Reconsider the number of descriptor pools (whether 1 is enough) and the descriptor numbers
* In function RoadSegmentFromTwoRoadNodes: the normalize function leads to values being Nan. Handle this in a better way.
* tile transform might need to be passed to shader as a uniform buffer

# features
* Triangulation
  * Find better solution for the global BVH in cesium layer
  * case where multiple roads overlapping same triangle is not handled.
  * clean up tile rendering pipeline after triangulation code added
  * (optional)Implement triangulation of terrain and road only
* Show a clock in the application allowing 
* Tesselation could be used in tile pipeline for road colormap overlays
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
