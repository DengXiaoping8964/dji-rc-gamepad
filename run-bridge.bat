@echo off
rem Daily launcher: DJI RC-N3 -> virtual Xbox 360 gamepad.
rem Works both from a source checkout (build\ subfolder) and from the release zip.
setlocal
cd /d "%~dp0"
set "EXE=dji-rc-bridge.exe"
if not exist "%EXE%" if exist "build\dji-rc-bridge.exe" set "EXE=build\dji-rc-bridge.exe"
if not exist "%EXE%" (
  echo dji-rc-bridge.exe not found - run build.bat first, or use the release zip.
  pause
  exit /b 1
)
"%EXE%" --run
