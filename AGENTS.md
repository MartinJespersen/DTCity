# Code Guidelines
- snake_case is used for variable and function names.
- A Unity Build is used for building the project for all source code that is not third-party. Including files should therefore not be done in every file like in standard C++. The includes should be put in includes.cpp or includes.hpp or in a layer include file (\*\_inc.cpp and \*\_inc.hpp).
- All functions should have a declaration in the header file and definition in the source file.
- Do not follow standard c++ guidelines, but try to get inspiration from the the repo itself and repos mentioned below.
- Try to minimize function calls to places where code is repeated. Not a strict rule though. 
- Instead of repeating a function call various places try to find a structure code to run that function a single place. Not a strict rule though. 
- I am trying to follow ZII (zero is initialization), which means the code should still work in cases where object are initialized to zero using e.g. using a function like PushStruct that zeroes memory when memory is allocated.
- Try to create pure functions as long as it makes sense, and try to make them testable. If they are testable then create tests in the test directory.

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
