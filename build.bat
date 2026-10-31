@echo off
rem Build helper. Everything stays on D: per the machine's storage rule.
setlocal
call "D:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
echo BUILD OK
