# 🏍️ Yamaha MT-09 SP (2026 / Euro 5+) CAN Bus Tool

A lightweight, high-performance CAN bus diagnostic tool and sniffer tailored for the **2024-2026 Yamaha MT-09 / MT-09 SP (Euro 5 / Euro 5+)** running on an ultra-compact **ESP32-C3 Super Mini Plus**.

---

## 🎯 Project Roadmap

| Phase | Milestone | Status | Description |
| :---: | :--- | :---: | :--- |
| **Phase 1** | **CAN Bus Sniffer & Discovery** |  **Current** | Safe passive listen-only sniffer, unique ID discovery, SavvyCAN SLCAN support, bus statistics. |
| **Phase 2** | **Exhaust DTC Reader & Auto-Clear** | ⏳ *Next* | Detect exhaust servo motor / O2 trouble codes (Akrapovič exhaust), auto-clear safely when engine is off (0 RPM). |
| **Phase 3** | **Logging, Export & Phone Live Telemetry** | ⏳ *Roadmap* | Log all DTC clear events to flash memory with export (CSV) and live telemetry stream (RPM, TPS, Coolant, Volts) on phone. |

---

## 🔌 Hardware Setup & Pinout

### 1. ESP32-C3 Super Mini Plus to CAN Transceiver (SN65HVD230 / VP230)

> [!IMPORTANT]
> Always use a **3.3V CAN transceiver** such as the **SN65HVD230** or **VP230**. Do **not** power a 5V transceiver (e.g., TJA1050 or MCP2551) with 5V directly to the ESP32 GPIOs without logic level shifters.

| ESP32-C3 Super Mini Pin | CAN Transceiver (SN65HVD230) | Description |
| :--- | :--- | :--- |
| `3V3` | `3V3` (VCC) | 3.3V Logic Power |
| `GND` | `GND` | Common Ground |
| `GPIO 21` | `TX` / `TXD` | TWAI / CAN Transmit |
| `GPIO 3` | `RX` / `RXD` | TWAI / CAN Receive |
| *(Internal USB D+/D-)* | USB-C Port | Native USB CDC Serial Console (115200 baud) |
| `GPIO 8` | Onboard Blue LED | Blinks on CAN packet reception (active LOW) |

---

### 2. Yamaha Euro 5 / Euro 5+ Diagnostic Connector (ISO 19689 Red 6-Pin)

Located underneath the seat of 2021+ Yamaha MT-09 models (Euro 5 red connector):

```
       [ Clip on top ]
     +-----------------+
     |  [1]   [2]   [3]|
     |  [4]   [5]   [6]|
     +-----------------+
```

| Pin # | Wire Color (Typical) | Function | Connection |
| :---: | :--- | :--- | :--- |
| **1** | Red / White | **+12V Battery** (Constant) | Optional 12V-to-5V step-down to power ESP32 |
| **2** | Black | **Ground (GND)** | Connect to ESP32 / Transceiver `GND` |
| **3** | Brown / Red | **+12V Switched** (Ignition ON) | Powers on with bike key |
| **4** | White / Black | **CAN High (CAN-H)** | Connect to Transceiver `CANH` |
| **5** | White / Blue | **CAN Low (CAN-L)** | Connect to Transceiver `CANL` |
| **6** | Yellow | K-Line (Diagnostic) | *Unused for CAN* |

> [!TIP]
> **120Ω Termination Resistor Note:**
> The motorcycle's CAN bus already contains termination resistors inside the factory ECUs. If your SN65HVD230 module has a 120Ω resistor (marked `R2` or `120R`) between `CANH` and `CANL`, in some cases it can cause high bus loading (total resistance drops from 60Ω to 40Ω). The safe **Listen-Only mode** will let you verify if frames are received cleanly.

---

## 🚀 Quickstart: Running the CAN Sniffer

### 1. Build and Flash
Flash via PlatformIO over the built-in USB-C port:
```powershell
pio run -t upload
```

### 2. Open Serial Monitor
Connect using any serial terminal or PlatformIO at **115200 baud**:
```powershell
pio device monitor -b 115200
```

On boot, you will be greeted by the interactive banner:
```text
=================================================================
  🏍️  YAMAHA MT-09 SP EURO 5+ CAN BUS SNIFFER
  Hardware: ESP32-C3 Super Mini Plus | TWAI CAN Controller
=================================================================
[INIT] CAN Controller initialized successfully on TX: GPIO 5, RX: GPIO 4
[INIT] Speed: 500 kbps (Euro 5 Standard) | Mode: LISTEN-ONLY (Safe Passive)
[INIT] Listening for traffic... Press '?' or 'h' for menu.
```

---

## ⌨️ Interactive Serial Commands

Type any of the following characters into the Serial Monitor and press Enter (or send raw keystroke):

| Command | Action | Description |
| :---: | :--- | :--- |
| `?` or `h` | **Help Menu** | Displays all available commands. |
| `m` | **Toggle Output Mode** | Switch between **Human-Readable** and **SLCAN** format (for SavvyCAN). |
| `l` | **Toggle Listen-Only** | Switch between safe passive listening (`LISTEN-ONLY`) and active ACK (`NORMAL`). |
| `b` | **Cycle Baud Rate** | Cycles through `500k` -> `250k` -> `1M` -> `125k` on the fly. |
| `u` | **Unique IDs Table** | Prints a table of all unique CAN IDs captured, their count, frequency (Hz), interval, and payload. |
| `s` | **Bus Statistics** | Shows hardware state, FPS, total frames, error counters, and buffer overflows. |
| `f <id>` | **Filter ID** | Focus stream only on a specific CAN ID (e.g., `f 7E8`). Send `f` with no ID to clear. |
| `p` | **Pause / Resume** | Pauses/resumes live stream output without dropping packet tracking. |
| `c` | **Clear / Reset** | Clears the screen and resets counters and discovered IDs. |

---

## 📊 Using with SavvyCAN / Wireshark (SLCAN Mode)

1. Connect the ESP32 to your PC via USB.
2. In the Serial Monitor, press `m` to enable **SLCAN Mode**.
3. In [SavvyCAN](https://www.savvycan.com/):
   - Go to **Connection** -> **Open Connection Window**.
   - Add new connection: select **SERIAL / COM Port** -> select your ESP32 COM port -> **Speed: 115200** or higher.
   - Device Type: **Lawicel / SLCAN**.
   - Click **Connect**.
4. You will now see real-time graphical graphs, packet decoders, and flow charts of all bike CAN communication!

---

## 🛡️ Safety Guarantee

By default, the firmware operates in **`TWAI_MODE_LISTEN_ONLY`**:
- The ESP32 does **not** drive the CAN bus dominant state.
- No acknowledgment (ACK) bits or error frames are transmitted onto the bike's bus.
- It is physically impossible for the sniffer to interfere with the bike's ABS, IMU, quickshifter, or ECU communication while in this mode.
