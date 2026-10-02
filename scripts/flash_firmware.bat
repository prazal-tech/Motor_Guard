@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM3

echo ========================================================
echo  Flashing Firmware to ESP32 on port %PORT%
echo ========================================================

cd /d "%~dp0\..\firmware\esp32"
idf.py -p %PORT% flash monitor
