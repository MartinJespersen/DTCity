::call vcvars64.bat
call build.bat
pushd .\build\msc\debug\
call C:\repos\raddebugger\build\raddbg.exe --auto_step -q city.exe
popd
