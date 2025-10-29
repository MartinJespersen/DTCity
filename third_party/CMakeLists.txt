cmake_minimum_required(VERSION 3.15)
project(DTCityThirdParty)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set(THIRD_PARTY_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIB_ARTIFACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

if (MSVC)
    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/win)
elseif (CMAKE_COMPILER_IS_GNUCXX)
    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/linux)
else()
    message(FATAL_ERROR "Unsupported compiler")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PLATFORM_LIB_ARTIFACT_DIR})

# Disable KTX tests to avoid C++20 compilation issues
set(KTX_FEATURE_TESTS OFF CACHE BOOL "Disable KTX tests" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "Disable KTX tools" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "Disable KTX documentation" FORCE)

add_subdirectory(${THIRD_PARTY_LIBS_DIR}/glfw)
add_subdirectory(${THIRD_PARTY_LIBS_DIR}/ktx)
