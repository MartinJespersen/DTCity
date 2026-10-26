# Install only the simulator's external dependency. Its other libraries are vendored.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../simulator" CACHE PATH "Simulator dependency manifest")
include("${CMAKE_CURRENT_LIST_DIR}/CityToolchain.cmake")
