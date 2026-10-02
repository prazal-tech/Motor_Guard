#!/usr/bin/env bash
set -e

echo "========================================================"
echo " Building INFRA-NEX Motor Guard ESP-IDF Firmware"
echo "========================================================"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}/../firmware/esp32"

idf.py set-target esp32
idf.py build
echo "[SUCCESS] Firmware build completed!"
