#!/bin/bash
set -e

stb_include_path="/home/martin/libraries/stb"
debug_dir="build/debug"
exec_name="vulkan_gui"
lib_name="entrypoint.so"
entrypoint_file_name="entrypoint.cpp"
main_file_name="main.cpp"

exec_full_path="${debug_dir}/${exec_name}"
entrypoint_lib_full_path="${debug_dir}/${lib_name}"
cxxflags="-Wall -Wextra -Werror -fsanitize=address -pedantic -Wconversion -Wsign-conversion -Wno-unused-function -Wno-missing-field-initializers -Wno-write-strings -Wno-class-memaccess -Wno-pedantic -maes -msse4"
cflags="-std=c++20 -g ${cxxflags}"
cwd_ldflags="-I. -I${stb_include_path}"
entrypoint_ldflags="-lglfw -lvulkan -lpthread -lX11 -lXxf86vm -lXrandr -lXi -lfreetype  ${cwd_ldflags}"
exec_ldflags="-lglfw -ldl ${cwd_ldflags}"

pushd ./shaders
./compile.sh
popd

mkdir -p ${debug_dir}

g++ ${cflags} -shared -fPIC -o ${entrypoint_lib_full_path} ${entrypoint_file_name} ${entrypoint_ldflags} $@
g++ -o ${exec_full_path} ${main_file_name} ${cflags} ${exec_ldflags} $@
