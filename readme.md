# 🏍️ Yamaha MT-09 SP (Euro 5 / Euro 5+) CAN Bus Tool & Cockpit Suite

[![PlatformIO CI Build](https://github.com/Tap202/Euro5Cleaner/actions/workflows/build.yml/badge.svg)](https://github.com/Tap202/Euro5Cleaner/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3%20Super%20Mini-orange.svg)](https://www.espressif.com/en/products/socs/esp32-c3)
[![CAN Speed: 500 kbps](https://img.shields.io/badge/CAN%20Bus-500%20kbps%20Euro%205%2B-green.svg)](#-hardware-setup--pinout)
[![Power: Deep Sleep <15µA](https://img.shields.io/badge/Deep%20Sleep-%3C%2015%20%C2%B5A-brightgreen.svg)](#-ultra-low-power-deep-sleep--can-rx-wakeup)

An autonomous, production-grade CAN bus diagnostic tool, real-time telemetry cockpit, and exhaust fault cleaner tailored for the **2021–2026+ Yamaha MT-09 / MT-09 SP (Euro 5 / Euro 5+ / CP3)** running on an ultra-compact **ESP32-C3 Super Mini Plus**.

Designed to live permanently under the passenger seat, it eliminates exhaust servo motor Check Engine Lights (CEL), decodes high-frequency Yamaha 6-axis IMU lean angles, logs ride telemetry to CSV, and serves a modern, distraction-free wireless cockpit to your smartphone—with zero battery drain when parked.

---

## ⚡ Key Highlights

* 🛡️ **100% Autonomous Headless Auto-Clear**:
  * Operates completely silently in the background—**zero user interaction or phone connection required**.
  * Automatically scans ECU for trouble codes 4 seconds after ignition key-ON and clears exhaust servo / O2 faults (e.g. `P0036`, `P0030`) caused by aftermarket exhausts (Akrapovič, Arrow, decat headers).
  * Automatically detects engine shutdown and runs a background scan/clear cycle 3 seconds later.
* 🔒 **Hardware-Enforced Engine-OFF Safety Interlock**:
  * Mode 04 clear frames are strictly locked out whenever the engine is running or RPM > 0.
* 🏍️ **MotoGP-Style Real-Time Lean Angle**:
  * Reverse-engineered high-speed (1,000 Hz) broadcast from the factory Yamaha 6-axis IMU (CAN ID `0x27C`).
  * Live degrees display (`◀ 38.5° L` / `42.1° R ▶`), animated tilting motorcycle avatar, and persistent **Max Left / Max Right** lean memory.
* 📊 **Ride Telemetry Logger & 1-Click CSV Export**:
  * High-frequency sampling of `RPM`, `Speed (MPH & KM/H)`, `Gear`, `Throttle (TPS %)`, `Coolant Temp (°C)`, `IAT (°C)`, `Battery Voltage (V)`, and `Lean Angle (°)`.
  * Instant 1-click browser download of `MT09_Ride_Data_YYYY-MM-DD.csv` for analysis in Excel or trackday data software.
* 🌙 **Ultra-Low Power Deep Sleep (< 15 µA)**:
  * Automatically enters deep sleep after 60 seconds of CAN silence.
  * Instant hardware wakeup via GPIO 3 (`CAN_RX`) on the first dominant start-of-frame bit when the ignition key is turned ON.
  * Safe for months of parking without draining the motorcycle battery.
* 📱 **Modern Wireless Cockpit (Standalone Wi-Fi AP & PWA)**:
  * Hosts open Wi-Fi network `MT09-SP-CAN` with instant Captive Portal at `http://192.168.4.1` (or `http://mt09.local`).
  * Hero tachometer (0–11.5k RPM), calculated transmission gear (`[N]`, `[1]`–`[6]`), 0–60 MPH launch timer, shift light strobing at 9,800+ RPM, and 1-tap high-contrast **Sunlight Mode**.
* 📶 **Seatless Wireless Web OTA Updates**:
  * Dual 1.9MB OTA bank partition scheme (`min_spiffs.csv`). Flash new firmware over Wi-Fi at `http://192.168.4.1/update` without removing seat or tools!
* 🖨️ **3D-Printable Weatherproof Subframe Enclosure**:
  * Parametric OpenSCAD CAD source and ready-to-slice binary STLs with subframe zip-tie mounting tabs, cable strain relief collar, and LED viewing port.

---

## 📱 Mobile Web Dashboard

Connect your phone to the bike's Wi-Fi network to access the cockpit without installing any third-party app:

1. Connect to Wi-Fi: **`MT09-SP-CAN`** (Open, no password).
2. Navigate to **`http://192.168.4.1`** (or **`http://mt09.local`**).
3. *(Optional)* Tap **Share $\to$ Add to Home Screen** in Safari / Chrome for a frameless native full-screen app experience.

```
+-------------------------------------------------------------+
|  [Wi-Fi Active]      YAMAHA MT-09 SP       [☀️ Sunlight]    |
|-------------------------------------------------------------|
|  [🛡️ Background Auto-Clear: ARMED]              [4 Cleared] |
|  [Engine: RUNNING]       [ECU: CLEAN]       [Battery: 14.2V]|
|-------------------------------------------------------------|
|      [GEAR: 3]                       [ 64 MPH ]             |
|   ||||||||||||||||||||||||||||||||||||||......  8,450 RPM   |
|   0        3K         6K         9K       11.5K [SHIFT 9.8K]|
|-------------------------------------------------------------|
|                 🏍️ REAL-TIME LEAN ANGLE                     |
|      Max Left: 44.2°      [ 38.5° R ▶ ]     Max Right: 47.1°|
|     [-60° ------------●----------------------- +60°]        |
|-------------------------------------------------------------|
| [TPS: 42%]  [Coolant: 84°C]  [0-60: 3.41s]  [Stator: 14.2V] |
|-------------------------------------------------------------|
| 📊 RIDE TELEMETRY LOGGER: 1,840 samples | 03:45             |
| [ ■ Stop Recording ]                  [ 💾 Export CSV ]     |
|-------------------------------------------------------------|
| [🔍 Manual Scan Codes]            [🧹 Clear Codes (Eng-OFF)]|
+-------------------------------------------------------------+
```

> [!TIP]
> **Standalone Desktop Simulator:** You can preview and test the dashboard on your computer without an ESP32 plugged in! Simply double-click [`dashboard_wifi.html`](file:///c:/EU5/Euro5Cleaner/dashboard_wifi.html) in your browser. The built-in simulator dynamically animates rev sweeps, cornering lean, shift lights, and CSV export.

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

1. **Auto-Sleep:** If no CAN traffic is detected for 60 seconds (ignition key switched OFF) and no Wi-Fi clients are connected, the firmware stops TWAI and Wi-Fi and enters Deep Sleep:
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

### Option 1: Build & Flash via PlatformIO (Recommended)

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

### Option 2: Seatless Wireless Over-The-Air (OTA) Update

Once flashed, you never need to plug a USB cable into the bike again:
1. Compile the firmware binary:
   ```bash
   pio run
   ```
   *(Binary generated at `.pio/build/esp32-c3-supermini/firmware.bin`)*
2. Connect phone/laptop to Wi-Fi **`MT09-SP-CAN`**.
3. Open **`http://192.168.4.1/update`** in any browser.
4. Select `firmware.bin` and click **Upload & Flash**. The ESP32 updates and reboots within 5 seconds!

---

## ⌨️ USB CDC Serial CLI Menu

Type any key into the Serial Monitor at **115200 baud** to interact directly with the CAN controller:

| Key | Function | Description |
| :---: | :--- | :--- |
| `?` or `h` | **Help Menu** | Prints interactive command reference. |
| `r` | **Scan DTCs** | Queries active & pending fault codes (Mode 03/07) and decodes plain-English text. |
| `k` | **Clear DTCs** | Safely transmits Mode 04 clear command to reset Check Engine Light (engine-off only). |
| `t` | **Snapshot** | Displays live RPM, speed, gear, TPS %, coolant temp, battery volts, and lean angle. |
| `s` | **Bus Health** | Prints CAN controller state, frames/sec (FPS), total frame count, and error counters. |
| `u` | **Unique IDs** | Prints discovered CAN ID table with frequencies (Hz), intervals, and hex payloads. |
| `a` | **Auto-Cleaner** | Toggles autonomous background auto-cleaning on/off. |
| `l` | **Mode Toggle** | Switches between `LISTEN-ONLY` (passive sniffer) and `NORMAL` (active OBD & ACK). |
| `b` | **Cycle Baud** | Cycles speed on the fly: `500k` $\to$ `250k` $\to$ `1M` $\to$ `125k`. |
| `p` | **Pause / Play** | Pauses terminal packet printing without losing background statistics. |
| `c` | **Reset** | Clears counters, unique ID table, and peak lean angle memory. |
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
