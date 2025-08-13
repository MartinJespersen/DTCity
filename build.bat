@echo off
:: Set paths and filenames
set "cwd=%cd:\=\\%\\"
set "debug_dir=%cwd%build\\msc\\debug\\"
set "exec_name=city.exe"
set "dll_name=entrypoint_temp.dll"
set "entrypoint_file_name=entrypoint.cpp"
set "main_file_name=main.cpp"
set "exec_full_path=%debug_dir%%exec_name%"
set "dll_full_path=%debug_dir%%dll_name%"
set "shader_path=%cwd%shaders\\"
set "tracy_src=%cwd%profiler\\TracyClient.cpp"
set "lib_dir=%cwd%third_party\\"
set "vulkan_path=C:\\VulkanSDK\\"
set "glfw_lib_dir=%lib_dir%glfw\\lib\\"
set "ktx_lib_dir=%lib_dir%ktx"
:: Compiler and linker flags
set "cxxflags=/W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7"
set "include_dirs=/I. /I%vulkan_path%Include /I%lib_dir%glfw\\include /I%lib_dir% /I%lib_dir%ktx\\include"
:: free typed is the debug lib

set "link_libs=glfw3dll.lib vulkan-1.lib gdi32.lib ktx_read.lib"
set "link_flags=/ignore:4099 /MACHINE:X64"
set "link_dirs=/LIBPATH:%glfw_lib_dir% /LIBPATH:%vulkan_path%Lib /LIBPATH:%ktx_lib_dir%"
set "glfw_dll_path=%glfw_lib_dir%glfw3.dll"

if not exist "%shader_path%" mkdir "%shader_path%"
:: Compile shaders
pushd shaders
call compile.bat
popd

:: Create debug directory
set debug_dir_single_quote=%debug_dir:\\=\%
if not exist "%debug_dir_single_quote%" mkdir "%debug_dir_single_quote%"

set exec_full_path_single_quote=%exec_full_path:\\=\%
if exist %exec_full_path_single_quote% del %exec_full_path_single_quote%


set glfw_dll_path_single_quote=%glfw_dll_path:\\=\%
copy %glfw_dll_path_single_quote% %debug_dir_single_quote%
:: Compile main executable

set ENTRYPOINT=cl /MD /fsanitize=address /D_USRDLL /D_WINDLL %entrypoint_file_name% %cxxflags%  %include_dirs% /nologo /link /DLL /OUT:%dll_full_path% %link_dirs% %link_libs% %link_flags% /INCREMENTAL:NO /noexp
set MAIN=cl /MD /fsanitize=address %main_file_name% %tracy_src% /Fe%exec_full_path%  %cxxflags%  %include_dirs% /DBUILD_CONSOLE_INTERFACE /nologo /link %link_dirs% %link_libs% %link_flags% /INCREMENTAL:NO /noexp

call %ENTRYPOINT%
call %MAIN%
echo %ENTRYPOINT%
echo %MAIN%
