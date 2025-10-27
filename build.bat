@echo off
:: Set paths and filenames
set "cwd=%cd:\=\\%\\"
set "src_dir=%cwd%src\\"
set "data_dir=%cwd%data\\"
set "debug_dir=%cwd%build\\msc\\debug\\"
set "exec_name=city.exe"
set "dll_name=entrypoint_temp.dll"
set "entrypoint_file_name=entrypoint.cpp"
set "main_file_name=src\\main.cpp"
set "exec_full_path=%debug_dir%%exec_name%"
set "dll_full_path=%debug_dir%%dll_name%"
set "shader_path=%data_dir%shaders\\"
set "lib_dir=%src_dir%third_party\\"
set "vulkan_path=%lib_dir%vulkan_sdk_v1_4_309_0\\"
set "tracy_src=%lib_dir%tracy/TracyClient.cpp"

:: Compiler and linker flags
set "cxxflags=/W4 /std:c++20 /wd4201 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7"
if not "%~1"=="" (
    set "cxxflags=%cxxflags% %~1"
)
set "include_dirs=/I. /I%vulkan_path%Include /I%lib_dir%glfw\\include /I%lib_dir% /I%lib_dir%ktx\\include"
set "link_flags=/ignore:4099 /MACHINE:X64 /NODEFAULTLIB:library"
set "link_dirs=/LIBPATH:%cwd%lib_artifacts\\win32"

if not exist "%shader_path%" mkdir "%shader_path%"
:: Compile shaders
pushd %shader_path%
call compile.bat
popd

:: Create debug directory
set debug_dir_single_quote=%debug_dir:\\=\%
if not exist "%debug_dir_single_quote%" mkdir "%debug_dir_single_quote%"

set exec_full_path_single_quote=%exec_full_path:\\=\%
if exist %exec_full_path_single_quote% del %exec_full_path_single_quote%

:: Compile main executable
set MAIN=cl /fsanitize=address /MD %main_file_name% %tracy_src% /Fe%exec_full_path% %cxxflags%  %include_dirs% /DBUILD_CONSOLE_INTERFACE /DBUILD_DEBUG /nologo /link %link_dirs% %link_flags% /INCREMENTAL:NO /noexp

call %MAIN%
echo %MAIN%
