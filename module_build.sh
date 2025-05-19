#!/bin/bash
set -e

compiler_flags="-Wall -Wextra -Werror -fsanitize=address -pedantic -Wconversion -Wsign-conversion -Wno-missing-field-initializers -Wno-write-strings -Wno-class-memaccess -Wno-pedantic -maes -msse4"
lld_paths="-I. -I./base -I./ui -I./third_party -I/home/martin/libraries/stb"
lld_flags="-lglfw -lvulkan -lpthread -lX11 -lXxf86vm -lXrandr -lXi -lfreetype  ${lld_paths}"
g++ -std=c++20 -c ${compiler_flags} ./base/base.cpp ${lld_paths} -include ./base/base.hpp
g++ -std=c++20 -c ${compiler_flags} ./ui/ui.cpp ${lld_flags} -include ./base/base.hpp -include ./ui/ui.hpp