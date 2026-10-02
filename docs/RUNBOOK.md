# 📖 ESP-IDF Runbook & Troubleshooting Guide

## 🚀 Flashing & Setup Instructions (ESP-IDF Terminal)

### Step 1: Open ESP-IDF Terminal
Ensure ESP-IDF environment variables are exported:
```bash
# Windows Command Prompt / PowerShell
export.bat

# Linux / macOS terminal
. $HOME/esp/esp-idf/export.sh
```

### Step 2: Navigate to Firmware Directory
```bash
cd firmware
```

### Step 3: Set Target Architecture
```bash
idf.py set-target esp32
```

### Step 4: Configure Wi-Fi Credentials
Edit [`firmware/main/config.h`](file:///c:/Users/praza/Desktop/Motor_Guard/firmware/main/config.h) or run `idf.py menuconfig` to set your local Wi-Fi SSID and Password:
```c
#define CONFIG_WIFI_SSID     "YOUR_WIFI_SSID"
#define CONFIG_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

### Step 5: Build, Flash, and Monitor
```bash
# Build firmware
idf.py build

# Flash to ESP32 (replace COM3 with your serial port)
idf.py -p COM3 flash monitor
```

---

## 🛠️ Troubleshooting Table

| Problem / Symptom | Possible Cause | Recommended Fix |
| :--- | :--- | :--- |
| **INA219 Warning in Log** | I2C SDA/SCL miswired or wrong address | Check SDA (GPIO 21) & SCL (GPIO 22). Verify address 0x40. |
| **DS18B20 Reads -999°C** | Missing pull-up resistor on GPIO 4 | Add 4.7k&Omega; pull-up resistor between 3.3V and GPIO 4 data line. |
| **RPM reads 0** | Hall sensor distance too far from magnet | Position magnet within 3–5mm of Hall sensor face. Check GPIO 27 connection. |
| **Wi-Fi fails to connect** | Incorrect SSID / WPA2 credentials | Check `CONFIG_WIFI_SSID` and `CONFIG_WIFI_PASSWORD` in [`config.h`](file:///c:/Users/praza/Desktop/Motor_Guard/firmware/main/config.h). |
| **Build Error: `idf.py not found`** | ESP-IDF environment not loaded | Run `export.bat` or `. export.sh` in terminal before executing `idf.py`. |
