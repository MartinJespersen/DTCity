# Problems:
* ASAN notice leak when freeing depth buffer with at swapchain recreation, but it only happens sometimes.
 * AddressSanitizer: attempting free on address which was not malloc()

* Device loss happens during swapchain recreation on AMD integrated graphics hardware

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
* explore dynamic renderpasses in vulkan to simplify rendering
* Change C namespacing to C++ namespacing
* remove snake case naming to pascal case for function along the way
* Reconsider global state for http, tctx, etc.
* Create hot reload enable/disable switch


# On Linux
* For Khronos validation layer support (enabled if DEBUG_BUILD macro is defined) remember to install it:
  * sudo apt install vulkan-validationlayers (Ubuntu)
* Compiling (incl. linking) takes a very long time at the moment.
* hotreloading does not work at the moment, due to the entrypoint library not being unloaded using dlclose
  * This has been a problem defining TRACY_ENABLE in the past as the library overwrites dlclose. This is however not the current problem
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Mule Test Program
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 mule.cpp /DBUILD_CONSOLE_INTERFACE
