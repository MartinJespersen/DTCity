// ~mgj: include imgui
#pragma push_macro("Swap")
#pragma push_macro("Min")
#pragma push_macro("Max")
#undef Swap
#undef Min
#undef Max
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#pragma pop_macro("Swap")
#pragma pop_macro("Min")
#pragma pop_macro("Max")
