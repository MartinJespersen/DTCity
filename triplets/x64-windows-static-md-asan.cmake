set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# The allocator must use the same ASan runtime as the MSVC application.
set(DTCITY_MIMALLOC_ASAN ON)
