#!/bin/bash
set -e

debug_dir="build/linux/debug"
exec_name="city"
lib_name="entrypoint.so"
entrypoint_file_name="entrypoint.cpp"
main_file_name="main.cpp"

exec_full_path="${debug_dir}/${exec_name}"
entrypoint_lib_full_path="${debug_dir}/${lib_name}"
cxxflags="-fsanitize=address -pedantic -Wno-unused-function -Wno-missing-field-initializers -Wno-sign-conversion -Wno-write-strings -Wno-class-memaccess -Wno-comment -Wno-pedantic -maes -msse4"
# remove flags: -w (disable warnings) and -fmax-errors (ceiling on errors)
cflags="-std=c++20 -w -fmax-errors=10 -g ${cxxflags}"
cwd_ldflags="-I. -Ithird_party -I./third_party/glfw/include -L./third_party/glfw/lib"
shared_ldflags="-lvulkan -lglfw -lpthread -ldl"
entrypoint_ldflags="${shared_ldflags} -lX11 -lXxf86vm -lXrandr -lXi ${cwd_ldflags}"
exec_ldflags="${shared_ldflags} ${cwd_ldflags}"

pushd ./shaders
./compile.sh
popd

mkdir -p ${debug_dir}

g++ ${cflags} -shared -fPIC -o ${entrypoint_lib_full_path} ${entrypoint_file_name} ${entrypoint_ldflags} $@
g++ -o ${exec_full_path} ${main_file_name} ${cflags} ${exec_ldflags} $@
