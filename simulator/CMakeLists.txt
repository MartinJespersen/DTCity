cmake_minimum_required(VERSION 3.16)

project(Simulator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

add_library(civetweb STATIC
    third_party/civetweb/civetweb.c
)

add_library(gl3w STATIC
    third_party/gl3w/src/gl3w.c
)

add_library(yyjson STATIC
    third_party/yyjson/yyjson.c
)

add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
)

target_compile_definitions(civetweb
    PUBLIC
        USE_WEBSOCKET
        NO_SSL
)

target_include_directories(civetweb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/civetweb
)

target_include_directories(gl3w
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gl3w/include
)

target_include_directories(yyjson
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
)

target_include_directories(sqlite3
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

target_link_libraries(sqlite3 PUBLIC Threads::Threads)

if(NOT WIN32)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(Simulator
    simulator.c
)

target_include_directories(Simulator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear
)

target_link_libraries(Simulator
    PRIVATE
        civetweb
        gl3w
        yyjson
        sqlite3
        OpenGL::GL
        Threads::Threads
)

if(NOT WIN32)
    target_link_libraries(Simulator PRIVATE m)
endif()

if(WIN32)
    target_link_libraries(Simulator PRIVATE ws2_32)
    target_compile_definitions(Simulator PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(civetweb PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(sqlite3 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(Simulator PRIVATE glfw)

if(MSVC)
    target_compile_options(Simulator PRIVATE /W4)
    target_compile_options(civetweb PRIVATE /W4)
    target_compile_options(gl3w PRIVATE /W4)
    target_compile_options(yyjson PRIVATE /W4)
else()
    target_compile_options(Simulator PRIVATE -O2 -Wall -Wextra)
    target_compile_options(civetweb PRIVATE -O2 -Wall -Wextra)
    target_link_libraries(gl3w PUBLIC ${CMAKE_DL_LIBS})
    target_compile_options(gl3w PRIVATE -O2 -Wall -Wextra)
    target_compile_options(yyjson PRIVATE -O2 -Wall -Wextra)
endif()
