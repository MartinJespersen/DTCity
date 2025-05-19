#include "io.cpp"
#include "algos.cpp"
#include "algos.hpp"
#include "core.cpp"
#ifdef _GNUC_ //TODO: create implementation for windows as well  
#include "time.cpp"
#endif
#include "third_party/third_party_wrapper.cpp"
#if defined(_MSC_VER)
    #include "os_win.cpp"
#elif defined(__linux__)
// TODO: create os folder for linux
#else
#error Libraries missing for current OS
#endif