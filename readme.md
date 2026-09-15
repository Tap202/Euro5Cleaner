# 🏍️ Yamaha MT-09 SP (Euro 5 / Euro 5+) CAN Bus Tool & Cockpit Suite

[![PlatformIO CI Build](https://github.com/Tap202/Euro5Cleaner/actions/workflows/build.yml/badge.svg)](https://github.com/Tap202/Euro5Cleaner/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3%20Super%20Mini-orange.svg)](https://www.espressif.com/en/products/socs/esp32-c3)
[![Web BLE Cockpit](https://img.shields.io/badge/Live%20Cockpit-GitHub%20Pages-00e5ff.svg)](https://tap202.github.io/Euro5Cleaner/)
[![Power: Deep Sleep <15µA](https://img.shields.io/badge/Deep%20Sleep-%3C%2015%20%C2%B5A-brightgreen.svg)](#-ultra-low-power-deep-sleep--can-rx-wakeup)

An autonomous, production-grade CAN bus diagnostic tool, real-time telemetry cockpit, and exhaust fault cleaner tailored for the **2021–2026+ Yamaha MT-09 / MT-09 SP (Euro 5 / Euro 5+ / CP3)** running on an ultra-compact **ESP32-C3 Super Mini Plus**.

Designed to live permanently under the passenger seat, it eliminates exhaust servo motor Check Engine Lights (CEL), decodes Yamaha ECU diagnostic trouble codes in real-time, logs clear history to persistent NVS flash memory, and pairs instantly with your smartphone over Bluetooth Low Energy (BLE)—with zero battery drain when parked.

---

## ⚡ Key Highlights

* 🛡️ **100% Autonomous Headless Auto-Clear**:
  * Operates completely silently in the background—**zero user interaction or phone connection required**.
  * Automatically scans ECU for trouble codes 4 seconds after ignition key-ON and clears exhaust servo / O2 faults (e.g. `P0036`, `P0030`) caused by aftermarket exhausts (Akrapovič, Arrow, decat headers).
  * Automatically detects engine shutdown and runs a background scan/clear cycle 3 seconds later.
* 💾 **Persistent Flash NVS Clear Logger**:
  * Automatically records every clear event to ESP32 Flash memory (`Preferences` NVS).
  * Logs boot count, uptime seconds, trigger source (`BOOT_AUTO`, `POST_RIDE`, `BLE_MANUAL`), codes cleared, and execution status.
  * Survives battery disconnections and reboots; retrievable at any time over BLE or Serial CLI.
* 🔒 **Hardware-Enforced Engine-OFF Safety Interlock**:
  * Mode 04 clear frames are strictly locked out whenever the engine is running or RPM > 0.
* 📱 **Bluetooth Low Energy (BLE 5.0) GATT Interface**:
  * Connects directly to smartphones with zero Wi-Fi network switching or internet interruption.
  * Hosted live on **GitHub Pages** (no file download needed): [**https://tap202.github.io/Euro5Cleaner/**](https://tap202.github.io/Euro5Cleaner/).
  * Works on **iOS (iPhone/iPad)** via **Bluefy** or **nRF Connect**, and on **Android/PC/Mac** via **Google Chrome** or **Edge**.
* 🌙 **Ultra-Low Power Deep Sleep (< 15 µA)**:
  * Automatically enters deep sleep after 60 seconds of CAN silence and BLE disconnection.
  * Instant hardware wakeup via GPIO 3 (`CAN_RX`) on the first dominant start-of-frame bit when the ignition key is turned ON.
  * Safe for months of parking without draining the motorcycle battery.
* 🖨️ **3D-Printable Weatherproof Subframe Enclosure**:
  * Parametric OpenSCAD CAD source and ready-to-slice binary STLs with subframe zip-tie mounting tabs, cable strain relief collar, and LED viewing port.

---

## 📱 Mobile BLE Cockpit & iOS Access Guide

### 🌐 Hosted Online via GitHub Pages (Zero Downloads Required)

You do **not** need to manually download or transfer HTML files to your phone. The BLE Cockpit is hosted for free on GitHub Pages over secure HTTPS:

👉 [**https://tap202.github.io/Euro5Cleaner/**](https://tap202.github.io/Euro5Cleaner/)

---

### 🍏 Accessing on iOS (iPhone / iPad)

> [!IMPORTANT]
> **Why Safari doesn't connect:** Apple's WebKit engine in standard Safari and Chrome for iOS intentionally does **not** support the Web Bluetooth API (`navigator.bluetooth`). 

You have two simple, free ways to use it on iOS:

#### Option A: Free Web BLE Browser (Recommended — Full Visual Dashboard)
1. Install **[Bluefy – Web BLE Browser](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)** (Free on the iOS App Store).
2. Open Bluefy and navigate to:
   ```
   https://tap202.github.io/Euro5Cleaner/
   ```
3. Tap **Connect BLE** $\to$ select **`MT09-Cleaner`**.
4. *(Optional)* Tap the **Share / Action** icon in Bluefy and choose **"Add to Home Screen"** to turn it into a full-screen, standalone app on your iPhone!

#### Option B: Generic BLE GATT App (nRF Connect)
1. Install **[nRF Connect for Mobile](https://apps.apple.com/app/nrf-connect-for-mobile/id1054362403)** (Free on the iOS App Store).
2. Scan and connect to **`MT09-Cleaner`** (Service UUID `000018F0-0000-1000-8000-00805F9B34FB`).
3. Tap the Download / Notify icon on:
   * **`18F1` (Status)**: Live JSON string with ECU state and engine status.
   * **`18F2` (DTCs)**: Live trouble codes and descriptions.
   * **`18F4` (Logs)**: NVS flash clear history entries.
4. Write UTF-8 text commands to **`18F3` (Command)**:
   * `SCAN` : Query ECU for confirmed & pending trouble codes.
   * `CLEAR`: Safely clear codes and reset Check Engine Light.
   * `LOGS` : Refresh stored NVS history.
   * `ERASE_LOGS`: Wipe flash history.

---

### 🤖 Accessing on Android, Windows, Mac & Linux

Simply open [**https://tap202.github.io/Euro5Cleaner/**](https://tap202.github.io/Euro5Cleaner/) directly in **Google Chrome** or **Microsoft Edge**:
1. Click **Connect BLE** $\to$ select **`MT09-Cleaner`** in the pairing dialog.
2. The cockpit will automatically subscribe to notifications and display real-time statuses and logs.

---

## 🔌 Hardware Setup & Pinout

### 1. ESP32-C3 Super Mini to 3.3V CAN Transceiver (SN65HVD230)

> [!IMPORTANT]
> Always use a **3.3V CAN transceiver** such as the **SN65HVD230** or **VP230**. Do **not** connect a 5V transceiver (e.g. MCP2551 / TJA1050) directly to the ESP32 GPIOs without logic level shifters.

| ESP32-C3 Super Mini Pin | SN65HVD230 Transceiver | Description |
| :--- | :--- | :--- |
| `3V3` | `3V3` (VCC) | 3.3V Logic Supply |
| `GND` | `GND` | System Ground |
| `GPIO 21` | `TX` / `TXD` | TWAI / CAN Transmit |
| `GPIO 3` | `RX` / `RXD` | TWAI / CAN Receive (RTC GPIO Wakeup Pin) |
| `GPIO 8` | *(Onboard LED)* | Active-LOW CAN packet activity indicator |
| `USB-C` | USB D+ / D- | Native CDC Serial Terminal (115200 baud) |

---

### 2. Yamaha Euro 5 Diagnostic Connector (ISO 19689 Red 6-Pin)

Located underneath the passenger seat of 2021+ Yamaha MT-09 / MT-09 SP models:

```
        [ Retention Clip Top ]
      +------------------------+
      |  [1]     [2]      [3]  |
      |  [4]     [5]      [6]  |
      +------------------------+
```

| Pin # | Wire Color (Typical) | Function | Connection |
| :---: | :--- | :--- | :--- |
| **1** | Red / White | **+12V Battery** (Constant) | Optional 12V-to-5V step-down for deep sleep |
| **2** | Black | **Ground (GND)** | Connect to ESP32 & Transceiver `GND` |
| **3** | Brown / Red | **+12V Switched** (Ignition ON) | Powers on with ignition key |
| **4** | White / Black | **CAN High (CAN-H)** | Connect to Transceiver `CANH` |
| **5** | White / Blue | **CAN Low (CAN-L)** | Connect to Transceiver `CANL` |
| **6** | Yellow | K-Line (Diagnostic) | *Unused for CAN* |

> [!NOTE]
> **Termination Resistor Notice:** The motorcycle factory harness already includes internal 120Ω bus termination. If your SN65HVD230 breakout includes an onboard 120Ω resistor (marked `R2` or `120R`), it can be desoldered or left in place if total bus resistance remains above 40Ω.

---

## 🌙 Ultra-Low Power Deep Sleep & CAN RX Wakeup

The ESP32-C3 consumes approximately **5–15 µA** in deep sleep, preventing motorcycle battery drain during long storage:

1. **Auto-Sleep:** If no CAN traffic is detected for 60 seconds (ignition key switched OFF) and no BLE clients are connected, the firmware stops TWAI and BLE and enters Deep Sleep:
   ```cpp
   esp_deep_sleep_enable_gpio_wakeup(1ULL << CAN_RX_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
   esp_deep_sleep_start();
   ```
2. **Instant Dominant Wakeup:** When the motorcycle key is turned ON, the first CAN Start-of-Frame (SOF) bit pulls `CAN_RX` (GPIO 3) dominant (LOW), instantly waking the MCU in milliseconds.
3. **Headless Execution:** Upon waking, the firmware automatically runs its 4-second boot scan and clears pending exhaust codes without rider intervention.

---

## 📦 3D-Printable Weatherproof Enclosure

The [`enclosure/`](enclosure/) directory provides a custom snap-fit case engineered to clamp securely to the MT-09 subframe:

| File | Format | Description |
| :--- | :--- | :--- |
| [`mt09_can_case.scad`](enclosure/mt09_can_case.scad) | OpenSCAD | Fully parametric source model for custom tolerances and dimensions. |
| [`mt09_case_base.stl`](enclosure/mt09_case_base.stl) | Binary STL | Base compartment with internal PCB cradle standoffs, cable strain relief collar, and dual subframe zip-tie wings. |
| [`mt09_case_lid.stl`](enclosure/mt09_case_lid.stl) | Binary STL | Snap-fit top cover with embossed MT-09 SP branding and LED light-pipe inspection window. |

### Recommended 3D Printing Parameters
* **Filament:** PETG, ABS, or ASA (UV & heat resistant up to 80°C+ under motorcycle seats).
* **Layer Height:** `0.20 mm`.
* **Infill:** `30% – 40% Gyroid`.
* **Perimeters:** `3 walls`.
* **Supports:** None required (engineered with self-supporting 45° overhangs).

---

## 🚀 Quickstart & Installation

### 1. Build & Flash Firmware via PlatformIO

1. Clone repository:
   ```bash
   git clone https://github.com/Tap202/Euro5Cleaner.git
   cd Euro5Cleaner
   ```
2. Connect your ESP32-C3 Super Mini via USB-C.
3. Compile and flash firmware:
   ```bash
   pio run -t upload
   ```
4. Open the Serial Monitor (115200 baud):
   ```bash
   pio device monitor -b 115200
   ```

---

### 2. Enable GitHub Pages (Host Web Cockpit in 30 Seconds)

The Web BLE Cockpit (`index.html`) can be hosted directly from this repository for free with zero maintenance:

1. Open your repository on GitHub: [**https://github.com/Tap202/Euro5Cleaner**](https://github.com/Tap202/Euro5Cleaner)
2. Go to **Settings** $\to$ **Pages** (in the left navigation sidebar).
3. Under **Build and deployment**:
   * **Source:** Select `Deploy from a branch`
   * **Branch:** Select `main`
   * **Folder:** Select `/ (root)`
   * Click **Save**.
4. Within ~1 minute, GitHub will publish your live dashboard at:
   ```
   https://tap202.github.io/Euro5Cleaner/
   ```
5. Open that URL on iOS (via [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)) or Android/PC (via Chrome) to control your MT-09 wirelessly!

---

## ⌨️ USB CDC Serial CLI Menu

Type any key into the Serial Monitor at **115200 baud** to interact directly with the CAN controller:

| Key | Function | Description |
| :---: | :--- | :--- |
| `?` or `h` | **Help Menu** | Prints interactive command reference. |
| `r` | **Scan DTCs** | Queries active & pending fault codes (Mode 03/07) and decodes plain-English text. |
| `k` | **Clear DTCs** | Safely transmits Mode 04 clear command to reset Check Engine Light (engine-off only). |
| `l` | **List Logs** | Displays table of persistent NVS clear events recorded in flash memory. |
| `e` | **Erase Logs** | Wipes stored clear history from NVS flash memory. |
| `a` | **Auto-Cleaner** | Toggles autonomous background auto-cleaning on/off. |
| `z` | **Deep Sleep** | Immediately puts ESP32 into ultra-low power deep sleep (wakes on CAN RX). |

---

## 🔬 Reverse-Engineered Yamaha MT-09 CAN Architecture

| CAN ID | Frequency | Description & Reverse-Engineered Payloads |
| :---: | :---: | :--- |
| **`0x751`** | 20 Hz | **Engine Running State**: Byte 7 == `0x04` indicates engine running; `0x00` engine stopped. |
| **`0x27C`** | 1,000 Hz | **Yamaha 6-Axis IMU**: Bytes 0–1 form a signed 16-bit big-endian integer representing roll/lean angle in 0.1° resolution. Muted on USB stream to prevent flooding. |
| **`0x7DF`** | Broadcast | **OBD-II Functional Request**: Transmits Mode 01 PID queries, Mode 03/07 DTC reads, and Mode 04 clears. |
| **`0x7E8`** | Response | **Engine ECU Diagnostic Response**: Responds with live powertrain PIDs and diagnostic trouble codes. |

---

## 🛡️ Safety Architecture

1. **Hardware-Enforced Engine-OFF Lockout:**
   Every Mode 04 clear request—whether triggered autonomously in the background, via the web dashboard, or through the USB CLI—passes through a strict safety interlock:
   ```cpp
   if (engineRunning || currentRpm > 0) {
       return false; // REJECTED: Never clear while engine is running
   }
   ```
2. **Listen-Only Safety Mode:**
   The CAN controller can be switched to `TWAI_MODE_LISTEN_ONLY` on the fly, guaranteeing zero dominant bus driving, zero ACK generation, and zero interference with ABS, IMU, or throttle-by-wire communications.

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

*Disclaimer: This project is an independent open-source development and is not affiliated with, endorsed by, or associated with Yamaha Motor Corporation. Always operate motorcycles safely and comply with local road vehicle emissions and safety regulations.*
