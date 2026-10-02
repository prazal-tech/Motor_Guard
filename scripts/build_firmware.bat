@echo off
echo ========================================================
echo  Building INFRA-NEX Motor Guard ESP-IDF Firmware
echo ========================================================

cd /d "%~dp0\..\firmware\esp32"
idf.py set-target esp32
idf.py build

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Firmware build completed!
) else (
    echo [ERROR] Firmware build failed.
)
