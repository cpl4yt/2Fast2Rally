@echo off
setlocal
set "PATH=C:\w64devkit\bin;C:\cmake\bin;%PATH%"

set "EXTRA="
if /i "%1"=="console" set "EXTRA=-DRALLY_SHOW_CONSOLE=ON"

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release %EXTRA% || exit /b 1
cmake --build build -j 8 || exit /b 1

echo.
echo Build complete: build\2Fast2Rally.exe
