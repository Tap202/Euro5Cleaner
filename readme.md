# 🏍️ MT-09 SP Auto DTC Cleaner & BLE Telemetry Logger

An intelligent, ESP32-based CAN bus tool designed specifically for the Yamaha MT-09 SP (and other modern OBD-II motorcycles). 

If you've installed an aftermarket exhaust or removed the exhaust servo, you are likely familiar with the persistent Check Engine Light (CEL). Instead of paying for a costly ECU flash just to disable the code, this tool lives on your bike's diagnostic port. It silently monitors for specific exhaust-related Diagnostic Trouble Codes (DTCs), waits until you turn the engine off (0 RPM) to ensure safety, and automatically clears them. 

Beyond clearing codes, it acts as a **Live BLE Telemetry Logger** and includes **Over-The-Air (OTA)** update capabilities.

---

## ✨ Features

* **Intelligent Auto-Clearing:** Detects target exhaust `P` codes. Waits for the engine to shut off (0 RPM) before sending Mode 04 (Clear DTCs) to prevent ECU lockups or stalling.
* **ISO-TP Multi-Frame Support:** Custom-built ISO 15765-2 (ISO-TP) multi-frame handler using the ESP-IDF TWAI driver to process long strings of DTCs without dropping packets.
* **Live BLE Telemetry:** Streams RPM, Coolant Temperature, Throttle Position (TPS), Intake Air Temp (IAT), and Battery Voltage to your smartphone via Bluetooth Low Energy (BLE).
* **Low-Voltage Alarm:** Automatically alerts your phone if stator/charging voltage drops below 12.5V while riding.
* **NVS History Tracking:** Logs cleared codes, boot counts, and engine uptime to the ESP32's non-volatile flash memory.
* **CSV Data Export:** Dump your historical DTC clear logs directly to a CSV format over BLE.
* **Wi-Fi OTA Updates:** Update the ESP32 firmware wirelessly without having to remove the seat or unplug the device.

---

## 🛠️ Hardware Requirements

1. **ESP32 Development Board** (e.g., ESP32 NodeMCU, WROOM-32).
2. **3.3V CAN Transceiver** (e.g., SN65HVD230). *Note: Do not use a 5V transceiver like the TJA1050 without logic level converters, as it will fry the ESP32.*
3. **Yamaha OBD-II Adapter** (typically a 4-pin or 6-pin to standard 16-pin OBD2, depending on your MT-09 year).
4. Jumper wires and an enclosure to protect it from the elements.

### Wiring / Pinout

| ESP32 Pin | CAN Transceiver | Description |
| :--- | :--- | :--- |
| `3V3` | `3V3` | Power for Transceiver |
| `GND` | `GND` | Common Ground |
| `GPIO 5` | `TX` / `TXD` | TWAI Transmit |
| `GPIO 4` | `RX` / `RXD` | TWAI Receive |

*Connect `CAN High (CANH)` and `CAN Low (CANL)` from the transceiver to the corresponding pins on your bike's diagnostic port.*

---
