@echo off
echo Cleaning project...

:: Временные папки сборки
if exist build rmdir /s /q build
if exist out rmdir /s /q out
if exist .vs rmdir /s /q .vs
if exist x64 rmdir /s /q x64
if exist Debug rmdir /s /q Debug
if exist Release rmdir /s /q Release

:: Папки с результатами
if exist bin rmdir /s /q bin
if exist installer rmdir /s /q installer

:: Файлы CMake
del /f /q CMakeCache.txt 2>nul
del /f /q *.sln 2>nul
del /f /q *.vcxproj 2>nul
del /f /q *.vcxproj.filters 2>nul
del /f /q *.vcxproj.user 2>nul

echo ✅ Cleaning is complete!
pause