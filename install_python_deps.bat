@echo off
chcp 1251 >nul
echo Установка Python зависимостей...

cd front

:: Устанавливаем зависимости
pip install requests
pip install PyQt5
pip install pyserial
pip install python-can
pip install obd

echo ✅ Python зависимости установлены!
pause