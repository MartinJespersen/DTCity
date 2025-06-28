@echo off
:: Set paths and filenames
set "cwd=%cd%\"
set "debug_dir=%cwd%build\msc\debug"
set "exec_name=city.exe"
set "entrypoint_file_name=entrypoint.cpp"
set "main_file_name=main.cpp"
set "exec_full_path=%debug_dir%\%exec_name%"
set "shader_path=%cwd%shaders\"
set "tracy_src=%cwd%profiler\TracyClient.cpp"
set "lib_dir=%cwd%\third_party\"
set "vulkan_path=C:\VulkanSDK\"

:: Compiler and linker flags
set "cxxflags=/W0 /std:c++20 /wd4201 /EHsc /Z7"
set "include_dirs=/I. /I%vulkan_path%Include /I%lib_dir%glfw\include /I%lib_dir%"
:: free typed is the debug lib
set "link_libs=glfw3_mt.lib vulkan-1.lib gdi32.lib"
set "link_flags=/ignore:4099 /MACHINE:X64"
set "link_dirs=/LIBPATH:"%lib_dir%glfw\lib" /LIBPATH:%vulkan_path%Lib"

if not exist "%shader_path%" mkdir "%shader_path%"
:: Compile shaders
pushd shaders
call compile.bat %cd%
popd

:: Create debug directory
if not exist "%debug_dir%" mkdir "%debug_dir%"

if exist %exec_full_path% del %exec_full_path%
:: Compile main executable
cl /fsanitize=address %main_file_name% %tracy_src% /Fe"%exec_full_path%"  %cxxflags%  %include_dirs% /DBUILD_CONSOLE_INTERFACE /nologo /link %link_dirs% %link_libs% %link_flags% /INCREMENTAL:NO /noexp
