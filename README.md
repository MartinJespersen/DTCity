# Things to do:

* Move vulkan helpers to lib_wrappers and refactor to function as a wrapper
  * This helps in the use of webgl in the future.
* Linux support:
  * HTTP client implementation
  * entrypoint caller currently implemented for windows should change to linux.
  * hot reloading refactor to simplify the implementation for linux
  * Base library fixes
  * Compile scripts for linux(build for source and shaders)
  * Clangd configuration needs to change
* explore dynamic renderpasses in vulkan to simplify rendering
* Change C namespacing to C++ namespacing
* remove snake case naming to pascal case for function along the way
* Reconsider global state for http, tctx, etc.
