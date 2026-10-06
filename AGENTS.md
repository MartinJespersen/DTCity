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
- All private functions should be placed at the bottom of the header file (declaration) and source file.
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
- Data in structs should always be placed at the top with methods, constructor and destructors below it.
- Always look for ways to merge code paths and avoid duplication of code.
- For long code snippets, make sure to add comments to code sections.
- For the most part use Assert instead of AssertAlways
- limit the number of memory arena's, and only create a new one when another arena does not have a similar lifetime.
- The frame loop should never wait for resources, task, etc. All blocking operations should be avoided.
- Use this pointer in methods accessing internal state 

# Decision making
- Always ask for me to elaborate if needed? E.g something is unclear or there are multiple ways to implement a feature.

# Important cleanup before committing 
- move types and declarations into the headers and additional create declarations of functions if not already done, but only if the functions and types are public otherwise move them to the top of the cpp file.
- Make suggestions for better naming of all symbols if you believe they do not capture the context.

# Code Suggestions
- When you find a name (e.g. variable or function) that is not concise or explanatory enough, then suggest a better name.

# Third party libraries
- Cesium Native library source code can be found at https://github.com/CesiumGS/cesium-native or C:/repos/cesium-native

# Bug fix suggestions
- Always looked at the git changes being tracked to easier identify bugs and other issues.

# Explanations
- When explaining things try doing it using the ASD-STE100 language specification.

# Permissions
- Never ask for read permission

# My Setup
- I mainly use windows.
- I use the Zed editor: https://zed.dev/docs

# Code Design Inspiration Repos
- Odin Language: https://github.com/odin-lang/Odin
- RAD debugger: https://github.com/EpicGamesExt/raddebugger
