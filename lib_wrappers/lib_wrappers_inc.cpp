// lib(s)
DISABLE_WARNINGS_PUSH
#include "third_party/simdjson/simdjson.cpp"
#define VMA_IMPLEMENTATION
#include "third_party/vk_mem_alloc.h"
#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"
DISABLE_WARNINGS_POP
// user defined
#include "lib_wrappers/json.cpp"
#include "lib_wrappers/cgltf.cpp"
#include "lib_wrappers/vulkan_common.cpp"
#include "lib_wrappers/vulkan.cpp"
#include "lib_wrappers/render.cpp"
