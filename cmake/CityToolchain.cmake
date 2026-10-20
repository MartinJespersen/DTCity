# Choose native compilers and dependencies before vcpkg and project() run.
# Cache defaults allow explicit command-line overrides.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_C_COMPILER cl CACHE STRING "City C compiler")
  set(CMAKE_CXX_COMPILER cl CACHE STRING "City C++ compiler")
  set(VCPKG_TARGET_TRIPLET x64-windows-static-md CACHE STRING "City dependency triplet")
  set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../triplets" CACHE STRING "City triplet overrides")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_C_COMPILER clang CACHE STRING "City C compiler")
  set(CMAKE_CXX_COMPILER clang++ CACHE STRING "City C++ compiler")
  set(VCPKG_TARGET_TRIPLET x64-linux CACHE STRING "City dependency triplet")
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
