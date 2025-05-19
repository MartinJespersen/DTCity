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


:: Compiler and linker flags
set "cxxflags=/W0 /std:c++20 /wd4201"
set "include_dirs=/I. /IC:\VulkanSDK\Include\ /IC:\glfw\include /IC:\freetype-2.13.2\include"
:: free typed is the debug lib
set "link_libs=glfw3_mt.lib vulkan-1.lib user32.lib gdi32.lib opengl32.lib shell32.lib freetype.lib"
set "link_flags=/ignore:4099 /MACHINE:X64"
set "link_dirs=/LIBPATH:C:\glfw\lib-vc2019\ /LIBPATH:"C:\freetype-windows-binaries\release static\vs2015-2022\win64" /LIBPATH:C:\VulkanSDK\Lib"

if not exist "%shader_path%" mkdir "%shader_path%"
:: Compile shaders
pushd shaders
call compile.bat
popd

:: Create debug directory
if not exist "%debug_dir%" mkdir "%debug_dir%"

:: Compile main executable
cl /Z7  %main_file_name% %entrypoint_file_name% %tracy_src% /Fe"%exec_full_path%"  %cxxflags%  %include_dirs% /DTRACY_ENABLE /DPROFILING_ENABLE /nologo /link %link_dirs% %link_libs% %link_flags% /INCREMENTAL:NO /noexp /NODEFAULTLIB:freetype.lib
