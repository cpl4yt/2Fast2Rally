@echo off
rem Builds a clean Release (no console window) and zips it into dist\.
setlocal
set "PATH=C:\w64devkit\bin;C:\cmake\bin;%PATH%"
set "VER=1.1.0"
set "OUT=dist\2Fast2Rally-v%VER%"

cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DRALLY_SHOW_CONSOLE=OFF || exit /b 1
cmake --build build-release -j 8 || exit /b 1

if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
copy /y build-release\2Fast2Rally.exe "%OUT%\" >nul
xcopy /e /i /q /y res "%OUT%\res" >nul
copy /y README.md "%OUT%\" >nul
copy /y PLAYTEST.md "%OUT%\" >nul
copy /y CHANGELOG.md "%OUT%\" >nul
copy /y LICENSE "%OUT%\" >nul 2>nul

powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath 'dist\2Fast2Rally-v%VER%-win64.zip' -Force" || exit /b 1
echo.
echo Packaged: dist\2Fast2Rally-v%VER%-win64.zip
