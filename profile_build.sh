#!/bin/bash
set -e

stb_include_path="/home/martin/libraries/stb"
profile_dir="build/profiling"
exec_name="vulkan_gui"
lib_name="entrypoint.so"
tracy_lib_name="tracy.o"

entrypoint_file_name="entrypoint.cpp"
main_file_name="main.cpp"
tracy_full_path=profiler/TracyClient.cpp

exec_full_path="${profile_dir}/${exec_name}"
entrypoint_lib_full_path="${profile_dir}/${lib_name}"
tracy_lib_full_path="${profile_dir}/${tracy_lib_name}"

cflags="-std=c++20 -O3 -Wno-write-strings -Wno-unused-result -maes -msse4 -DDEBUG_BUILD"
cwd_ldflags="-I. -I${stb_include_path}"
entrypoint_ldflags="-lglfw -lvulkan -lpthread -lX11 -lXxf86vm -lXrandr -lXi -lfreetype ${cwd_ldflags}"
exec_ldflags="-lglfw -ldl ${cwd_ldflags}"
tracy_ldflags="${cwd_ldflags}"

pushd ./shaders
./compile.sh
popd

mkdir -p ${profile_dir}
g++ -c ${cflags} -o ${tracy_lib_full_path} ${tracy_full_path} ${tracy_ldflags} -DTRACY_ENABLE
g++ -c ${cflags} -o ${entrypoint_lib_full_path}  ${entrypoint_file_name} ${entrypoint_ldflags} -DTRACY_ENABLE -DPROFILING_ENABLE
g++ -o ${exec_full_path} ${main_file_name} ${entrypoint_lib_full_path} ${tracy_lib_full_path} ${cflags} ${exec_ldflags} ${entrypoint_ldflags} -DPROFILING_ENABLE
