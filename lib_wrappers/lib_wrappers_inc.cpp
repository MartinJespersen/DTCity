// lib(s)
DISABLE_WARNINGS_PUSH
#include "third_party/simdjson/simdjson.cpp"
#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"
DISABLE_WARNINGS_POP
// user defined
#include "lib_wrappers/json.cpp"
#include "lib_wrappers/cgltf.cpp"
