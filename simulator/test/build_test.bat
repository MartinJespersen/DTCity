@echo off
set "test_dir=%cd%\\output"
set "cur_dir=%cd%"
pushd ..
pushd ..
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 /I%cd% /Fe%test_dir%\\test.exe /Fo%test_dir%\\test.obj %cur_dir%\\test_main.cpp /DBUILD_CONSOLE_INTERFACE
popd
popd
