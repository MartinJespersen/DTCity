# System explanation
In the src directory all sub-directories are layers that are all responsible for their own memory in the form of memory arenas.
# Code Guidelines
- snake_case is used for variable and function names.
- A Unity Build is used for building the project for all source code that is not third-party. Including files should therefore not be done in every file like in standard C++. The includes should be put in includes.cpp or includes.hpp or in a layer include file (\*\_inc.cpp and \*\_inc.hpp).
- All functions should have a declaration in the header file and definition in the source file.
- Do not follow standard c++ guidelines, but try to get inspiration from the the repo itself and repos mentioned below.
- Try to minimize function calls to places where code is repeated. Not a strict rule though. 
- Instead of repeating a function call various places try to find a structure code to run that function a single place. Not a strict rule though. 
- I am trying to follow ZII (zero is initialization), which means the code should still work in cases where object are initialized to zero using e.g. using a function like PushStruct that zeroes memory when memory is allocated.
- Try to create pure functions as long as it makes sense, and try to make them testable. If they are testable then create tests in the test directory.
- Try to use the Arena in the base layer for memory allocations. When a library is used, check the library for any custom allocator hooks.
- Avoid making small function that just returns a single value. Instead just create a variable.
- You should never sleep at any point as it will block other stuff that needs to run. Find another way or cry to me about it.
- functions that are not used outside a namespace (private) should have a name starting with an underscore.
- All private functions should be placed at the bottom of the header file (declaration).
- When opening a file or other resource that needs a close immediatly use the defer function.
- Make sure to use the base layer or os_core functions as when needed.
- When you make changes to one OS in the os_core layers always make sure the changes work for the other OS present.
- Logs are produced in debug directory (located at build/<os>/<build_type>) that you should use for finding bugs.
- function names should have there name prefixes with <namespace>_ if the function is local to a layer it should be preceded by _<namespace>_.
- Be careful about using defensive programming and do not use it everywhere.
- Do not call functions inline for using the output. Always call the functions on a separate line.
- You should avoid returning nullptr except when the pointer is used to iterate over such as a linked list or stack. In this case a nullptr will just result in zero iterations in the caller.
- struct should always be placed in the header file (.h/.hpp)
- ScratchScope should always be created with the arena passed to the function, e.g. `ScratchScope scratch = ScratchScope(&arena, 1);`, to avoid arena collisions.

# Third party libraries
- Cesium Native library source code can be found at https://github.com/CesiumGS/cesium-native or C:/repos/cesium-native

# Bug fix suggestions
- Always looked at the git changes being tracked to easier identify bugs and other issues.

# Permissions
- Never ask for read permission
- Make your preferred changes instead of writing it in the chat.

# My Setup
- I mainly use windows and rarely linux.
- I use the Zed editor: https://zed.dev/docs

# Code Design Inspiration Repos
- Odin Language: https://github.com/odin-lang/Odin
- RAD debugger: https://github.com/EpicGamesExt/raddebugger
