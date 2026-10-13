# Choose native simulator compilers before project() enables C and C++.
# Cache defaults allow explicit compiler overrides from the command line.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_C_COMPILER cl CACHE STRING "Simulator C compiler")
  set(CMAKE_CXX_COMPILER cl CACHE STRING "Simulator C++ compiler")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_C_COMPILER clang CACHE STRING "Simulator C compiler")
  set(CMAKE_CXX_COMPILER clang++ CACHE STRING "Simulator C++ compiler")
endif()
