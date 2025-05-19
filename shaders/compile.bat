@echo off

:: Exit on error
setlocal

set path_dir=C:\VulkanSDK\1.4.309.0\Bin\
:: Compile shaders
call %path_dir%glslc shader.vert -o vert.spv
call %path_dir%glslc shader.frag -o frag.spv
call %path_dir%glslc text.vert -o text_vert.spv
call %path_dir%glslc text.frag -o text_frag.spv

endlocal