@echo off
chcp 65001 >nul
title ECU Diagnostic System

echo ==========================================
echo     ЗАПУСК ECU DIAGNOSTIC SYSTEM
echo ==========================================
echo.

:: Создание папки для логов
if not exist data mkdir data
if not exist data\logs mkdir data\logs

:: Проверка наличия сервера
if not exist bin\ecu_backend.exe (
    echo [ОШИБКА] Сервер не найден. Запустите build_main.bat сначала
    pause
    exit /b 1
)

:: Проверка базы данных
if not exist ecu_diagnostic.db (
    echo [ВНИМАНИЕ] База данных не найдена. Будет создана новая...
)

:: Запуск сервера в фоне
echo [1/3] Запуск сервера...
start /min bin\ecu_backend.exe
timeout /t 2 /nobreak >nul

:: Проверка что сервер запустился
tasklist | findstr ecu_backend.exe >nul
if %errorlevel% equ 0 (
    echo [OK] Сервер запущен (PID: 
    for /f "tokens=2" %%a in ('tasklist ^| findstr ecu_backend.exe') do echo %%a
) else (
    echo [ВНИМАНИЕ] Сервер не отвечает, но продолжаем...
)

:: Проверка доступности API
echo [2/3] Проверка доступности сервера...
set RETRY_COUNT=0
:CHECK_API
timeout /t 1 /nobreak >nul
curl -s http://localhost:8080/api/connection/status >nul 2>&1
if %errorlevel% neq 0 (
    set /a RETRY_COUNT+=1
    if %RETRY_COUNT% lss 5 (
        goto CHECK_API
    ) else (
        echo [ВНИМАНИЕ] Сервер не отвечает на API, но продолжаем...
    )
) else (
    echo [OK] Сервер отвечает на запросы
)

:: Запуск фронтенда
echo [3/3] Запуск графического интерфейса...
cd front
start python mainf.py
cd ..

echo.
echo ==========================================
echo ✅ ПРОГРАММА ЗАПУЩЕНА!
echo ==========================================
echo Сервер: http://localhost:8080
echo База данных: ecu_diagnostic.db
echo Логи: data\logs\
echo.
echo Для остановки закройте все окна
echo ==========================================

pause