@echo off
chcp 1251 >nul
title Building ECU Diagnostic Server

echo ==========================================
echo  Building ECU Diagnostic Server
echo ==========================================

:: ---------- Инициализация окружения VS ----------
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if not defined VSCMD_VER (
    if exist "%VCVARS%" (
        echo Инициализация Visual Studio 2026...
        call "%VCVARS%"
    ) else (
        echo [ОШИБКА] Visual Studio 2026 не найдена.
        echo Ожидаемый путь: %VCVARS%
        pause
        exit /b 1
    )
)

:: ---------- Проверка SQLite3 ----------
if not exist "sqlite3\include\sqlite3.h" (
    echo [ОШИБКА] SQLite3 не найден!
    echo Скачайте sqlite3.h и sqlite3.lib с https://www.sqlite.org/download.html
    echo и положите в:
    echo   sqlite3\include\sqlite3.h
    echo   sqlite3\lib\sqlite3.lib
    pause
    exit /b 1
)

:: ---------- Поиск CMake ----------
set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    :: Попробуем cmake из PATH
    where cmake >nul 2>&1
    if %errorlevel% == 0 (
        set "CMAKE_EXE=cmake"
    ) else (
        echo [ОШИБКА] cmake.exe не найден!
        pause
        exit /b 1
    )
)
echo CMake: %CMAKE_EXE%

:: ---------- Очистка и создание папок ----------
if exist build rmdir /s /q build
if exist bin   rmdir /s /q bin
mkdir build
mkdir bin

cd build

:: ---------- Конфигурация ----------
echo.
echo Конфигурация CMake...
"%CMAKE_EXE%" .. -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_TEST=OFF

if %errorlevel% neq 0 (
    echo.
    echo [ОШИБКА] Конфигурация не удалась
    cd ..
    pause
    exit /b 1
)

:: ---------- Компиляция ----------
echo.
echo Компиляция...
"%CMAKE_EXE%" --build . --config Release -- /m /nologo

if %errorlevel% neq 0 (
    echo.
    echo [ОШИБКА] Компиляция не удалась
    cd ..
    pause
    exit /b 1
)

cd ..

:: ---------- Копирование EXE ----------
copy "build\Release\ecu_backend.exe" "bin\" >nul 2>&1
if not exist "bin\ecu_backend.exe" (
    :: MSBuild иногда кладёт в другое место
    copy "build\ecu_backend.exe" "bin\" >nul 2>&1
)

:: ---------- Результат ----------
echo.
if exist "bin\ecu_backend.exe" (
    echo ==========================================
    echo  BUILD SUCCESSFUL
    echo ==========================================
    echo Файл: bin\ecu_backend.exe
    dir "bin\ecu_backend.exe" | findstr "ecu_backend"
) else (
    echo [ОШИБКА] ecu_backend.exe не найден после сборки
    echo Ищем EXE в build\...
    dir /s /b "build\*.exe" 2>nul
)

echo ==========================================
pause
