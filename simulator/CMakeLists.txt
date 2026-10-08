cmake_minimum_required(VERSION 3.16)

project(Simulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRACY_PROFILE_ENABLE "Enable Tracy profiler" OFF)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Mimalloc.cmake)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(NOT TARGET glfw)
    add_subdirectory(third_party/glfw)
endif()

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

target_compile_definitions(sqlite3
    PRIVATE
        SQLITE_ENABLE_RTREE
)

target_link_libraries(sqlite3 PUBLIC Threads::Threads)

if(NOT WIN32)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(Simulator
    simulator.c
)
set_source_files_properties(simulator.c PROPERTIES LANGUAGE CXX)
set_target_properties(Simulator PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_property(TARGET Simulator PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

if(TRACY_PROFILE_ENABLE)
    target_sources(Simulator PRIVATE ../src/third_party/tracy/TracyClient.cpp)
    target_compile_definitions(Simulator PRIVATE TRACY_ENABLE TRACY_PROFILE_ENABLE)
endif()

target_include_directories(Simulator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear
)

get_filename_component(DTCITY_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
target_compile_definitions(Simulator
    PRIVATE
        BUILD_CONSOLE_INTERFACE=1
        DTCITY_PROJECT_ROOT="${DTCITY_PROJECT_ROOT}"
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
    target_link_libraries(Simulator PRIVATE comdlg32 ws2_32)
    target_compile_definitions(Simulator PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(civetweb PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(sqlite3 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(Simulator PRIVATE glfw)
target_link_libraries(Simulator PRIVATE dtcity_mimalloc)

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
