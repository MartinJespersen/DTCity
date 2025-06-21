::call vcvars64.bat
set "cwd=%cd%"
call build.bat
pushd .\build\msc\debug\
:: call C:\repos\raddebugger\build\raddbg.exe --auto_step --project:%cwd% --ipc city.exe
:: call C:\repos\raddebugger\build\raddbg.exe --ipc find_code_location "C:/repos/DTCity/main.cpp:58:1"
popd

call C:\repos\raddebugger\build\raddbg.exe --ipc run
