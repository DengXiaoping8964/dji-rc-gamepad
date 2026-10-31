@echo off
rem 32-bit build: DJI's user-mode libusb stack ships as x86 DLLs, so the bulk
rem transport probe has to be x86 to load them.
setlocal
call "D:\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul || exit /b 1
set "PATH=D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%~dp0"
cmake -S . -B build_x86 -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build_x86 || exit /b 1
echo BUILD X86 OK
