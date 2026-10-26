# Choose native compilers and dependencies before vcpkg and project() run.
# Cache defaults allow explicit command-line overrides.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_C_COMPILER cl CACHE STRING "City C compiler")
  set(CMAKE_CXX_COMPILER cl CACHE STRING "City C++ compiler")
  set(city_triplet x64-windows-static-md)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_C_COMPILER clang CACHE STRING "City C compiler")
  set(CMAKE_CXX_COMPILER clang++ CACHE STRING "City C++ compiler")
  set(city_triplet x64-linux)
endif()

if(ASAN_ENABLED)
  string(APPEND city_triplet "-asan")
endif()
set(VCPKG_TARGET_TRIPLET "${city_triplet}" CACHE STRING "City dependency triplet")
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../triplets" CACHE STRING "City triplet overrides")

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
