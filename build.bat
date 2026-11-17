@echo off
rem Builds AutoVolume (Release) into bin\AutoVolume.exe and runs the unit tests.
rem Needs Visual Studio 2022+ (Desktop development with C++) and CMake 3.21+.
setlocal
cd /d "%~dp0"

cmake -S . -B build -A x64 || goto :error
cmake --build build --config Release --parallel || goto :error
ctest --test-dir build -C Release --output-on-failure || goto :error

echo.
echo Built bin\AutoVolume.exe
if /i "%~1"=="--shortcut" bin\AutoVolume.exe --install-shortcut && echo Desktop shortcut created.
exit /b 0

:error
echo.
echo Build failed.
exit /b 1
