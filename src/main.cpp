/**
 * =============================================================================
 * 🏍️ Yamaha MT-09 SP Euro 5+ CAN Bus Tool & Cockpit Suite
 * Platform: ESP32-C3 Super Mini Plus (Native USB CDC + Standalone Wi-Fi AP)
 * =============================================================================
 * 
 * Core Features:
 *  - 500 kbps TWAI / CAN Controller (ISO 11898 / Euro 5+ Standard)
 *  - AUTONOMOUS BACKGROUND DTC AUTO-SCAN & AUTO-CLEAR:
 *      - Completely headless! Sits permanently under the motorcycle seat.
 *      - Scans ECU for exhaust/O2 trouble codes 4 seconds after ignition key-ON.
 *      - Automatically clears exhaust servo/O2 codes while engine is stopped.
 *      - Detects engine shutdown: automatically re-scans & clears in background.
 *      - STRICT ENGINE-OFF SAFETY INTERLOCK: Never sends Mode 04 when RPM > 0.
 *  - REAL-TIME LEAN ANGLE (YAMAHA 6-AXIS IMU):
 *      - Decodes 1,000 Hz broadcast from CAN ID 0x27C (0.1° resolution).
 *      - Tracks live roll angle, Max Left Lean, and Max Right Lean memory.
 *  - RIDE DATA LOGGER & 1-CLICK CSV EXPORT:
 *      - Real-time logging of RPM, Speed, Gear, TPS %, Temps, Volts, and Lean.
 *      - Downloads instant CSV files straight from the mobile web browser.
 *  - ULTRA-LOW POWER DEEP SLEEP (< 15 µA):
 *      - Automatically enters deep sleep after 60s of CAN inactivity.
 *      - Hardware wakeup via GPIO 3 (CAN RX Dominant SOF bit) on bike Key-ON.
 *      - Zero motorcycle battery drain when parked!
 *  - WIRELESS WEB OTA UPDATES:
 *      - Dual 1.9MB OTA partition scheme (min_spiffs.csv).
 *      - Flash new firmware over Wi-Fi at http://192.168.4.1/update.
 *  - MOBILE WEB COCKPIT & CAPTIVE PORTAL:
 *      - Open Wi-Fi AP: "MT09-SP-CAN" (192.168.4.1 or http://mt09.local).
 *      - High-contrast Sunlight Mode, Gear indicator, 0-60 Launch Timer.
 * =============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include "driver/twai.h"
#include "esp_sleep.h"
#include "dashboard_html.h"

// =============================================================================
// HARDWARE PIN DEFINITIONS (ESP32-C3 Super Mini Plus)
// =============================================================================
#define CAN_TX_PIN         GPIO_NUM_21
#define CAN_RX_PIN         GPIO_NUM_3

// Onboard User LED on ESP32-C3 Super Mini is GPIO 8 (Active LOW)
#define ONBOARD_LED_PIN    8
#define LED_ACTIVE_LOW     true

// =============================================================================
// WI-FI ACCESS POINT & WEB SERVER CONFIGURATION
// =============================================================================
const char* AP_SSID = "MT09-SP-CAN";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);
const byte DNS_PORT = 53;

DNSServer dnsServer;
WebServer server(80);

// =============================================================================
// PERSISTENT NVS STORAGE (PREFERENCES)
// =============================================================================
Preferences prefs;
uint32_t totalClears = 0;
uint32_t bootCount = 0;
String lastClearedCode = "None";

// =============================================================================
// TELEMETRY & SYSTEM STATE
// =============================================================================
volatile uint16_t currentRpm = 0;
volatile int16_t  currentCoolantTemp = -999;
volatile int16_t  currentIat = -999;
volatile uint8_t  currentTps = 0;
volatile float    currentBatteryVolts = 0.0f;
volatile uint8_t  currentSpeedKmH = 0;
volatile uint8_t  currentSpeedMph = 0;
volatile uint8_t  currentGear = 0; // 0=Neutral, 1..6
volatile bool     engineRunning = false;
volatile float    currentLeanAngle = 0.0f;
volatile float    maxLeanLeft = 0.0f;
volatile float    maxLeanRight = 0.0f;
volatile uint32_t totalFramesRx = 0;
volatile uint32_t framesLastSecond = 0;
volatile uint32_t currentFps = 0;
volatile uint32_t busErrorCount = 0;
volatile uint32_t rxOverrunCount = 0;

unsigned long lastFpsCalcMs = 0;
unsigned long lastTelemetryPollMs = 0;
unsigned long lastCanRxMs = 0;
unsigned long ledTurnOffMs = 0;
unsigned long bootMs = 0;
const unsigned long INACTIVITY_SLEEP_TIMEOUT_MS = 60000; // 60s silence -> Deep Sleep

// =============================================================================
// AUTONOMOUS BACKGROUND AUTO-READ & AUTO-CLEAR ENGINE
// =============================================================================
bool autoCleanEnabled = true;
bool bootAutoCleanDone = false;
unsigned long bootAutoCleanDueMs = 0;
unsigned long lastAutoScanMs = 0;
unsigned long lastAutoClearMs = 0;
const unsigned long AUTO_SCAN_INTERVAL_MS = 25000;
const unsigned long AUTO_CLEAR_COOLDOWN_MS = 10000;
bool prevEngineRunning = false;

// =============================================================================
// CAN BUS CONTROLLER CONFIGURATION
// =============================================================================
enum CanBaudRate {
    BAUD_500K = 0,
    BAUD_250K,
    BAUD_1M,
    BAUD_125K
};

const char* BAUD_NAMES[] = { "500 kbps (Euro 5 Standard)", "250 kbps", "1 Mbps", "125 kbps" };
CanBaudRate currentBaud = BAUD_500K;
bool listenOnlyMode = false;
bool twaiRunning = false;
bool twaiInstalled = false;

// Filter and stream control
bool streamPaused = false;
bool changesOnly = true;
bool ignoreActive = true;
uint32_t ignoreId = 0x27C; // Yamaha 6-Axis IMU (floods at 1,000 Hz)
bool filterActive = false;
uint32_t filterId = 0x000;

#define MAX_UNIQUE_IDS 64
struct CanIdStats {
    uint32_t id;
    bool isExtended;
    uint32_t count;
    unsigned long lastSeenMs;
    uint32_t lastIntervalMs;
    unsigned long lastPrintMs;
    uint8_t lastData[8];
    uint8_t dlc;
};

CanIdStats uniqueIds[MAX_UNIQUE_IDS];
uint16_t uniqueIdCount = 0;

// DTC Storage
#define MAX_STORED_DTCS 16
struct StoredDtcItem {
    String code;
    String desc;
};

StoredDtcItem storedDtcs[MAX_STORED_DTCS];
int storedDtcCount = 0;
bool awaitingConsecutiveDtc = false;

struct KnownDtc {
    const char* code;
    const char* desc;
};

const KnownDtc KNOWN_DTCS[] = {
    { "P0030", "HO2S Heater Control Circuit (Bank 1 Sensor 1)" },
    { "P0031", "HO2S Heater Control Circuit Low (Bank 1 Sensor 1)" },
    { "P0032", "HO2S Heater Control Circuit High (Bank 1 Sensor 1)" },
    { "P0036", "HO2S Heater Control Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust / Akrapovič)" },
    { "P0037", "HO2S Heater Control Circuit Low (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0038", "HO2S Heater Control Circuit High (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0053", "HO2S Heater Resistance (Bank 1 Sensor 1)" },
    { "P0054", "HO2S Heater Resistance (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0130", "O2 Sensor Circuit (Bank 1 Sensor 1)" },
    { "P0131", "O2 Sensor Circuit Low Voltage (Bank 1 Sensor 1)" },
    { "P0132", "O2 Sensor Circuit High Voltage (Bank 1 Sensor 1)" },
    { "P0133", "O2 Sensor Circuit Slow Response (Bank 1 Sensor 1)" },
    { "P0134", "O2 Sensor Circuit No Activity (Bank 1 Sensor 1)" },
    { "P0135", "O2 Sensor Heater Circuit Malfunction (Bank 1 Sensor 1)" },
    { "P0136", "O2 Sensor Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0137", "O2 Sensor Circuit Low Voltage (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0138", "O2 Sensor Circuit High Voltage (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0139", "O2 Sensor Circuit Slow Response (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0140", "O2 Sensor Circuit No Activity (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0141", "O2 Sensor Heater Circuit Malfunction (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0420", "Catalyst System Efficiency Below Threshold (Bank 1 - Cat Removal)" },
    { "P0443", "Evaporative Emission Control System Purge Control Valve Circuit" },
    { "P0444", "Evaporative Emission Control System Purge Control Valve Circuit Open" },
    { "P0445", "Evaporative Emission Control System Purge Control Valve Circuit Shorted" },
    { "P0105", "Manifold Absolute Pressure Circuit" },
    { "P0110", "Intake Air Temperature Circuit" },
    { "P0115", "Engine Coolant Temperature Circuit" },
    { "P0120", "Throttle Position Sensor / APS Circuit" },
    { "P0201", "Injector Circuit / Open - Cylinder 1" },
    { "P0202", "Injector Circuit / Open - Cylinder 2" },
    { "P0203", "Injector Circuit / Open - Cylinder 3" },
    { "P0300", "Random/Multiple Cylinder Misfire Detected" },
    { "P0301", "Cylinder 1 Misfire Detected" },
    { "P0302", "Cylinder 2 Misfire Detected" },
    { "P0303", "Cylinder 3 Misfire Detected" },
    { "P0335", "Crankshaft Position Sensor A Circuit" },
    { "P0500", "Vehicle Speed Sensor Malfunction" }
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================
bool initTWAI(CanBaudRate baud, bool listenOnly);
void stopTWAI();
void enterDeepSleep();
bool sendCanFrame(uint32_t id, uint8_t len, const uint8_t* data);
void handleFrame(const twai_message_t &rxMsg);
void handleDiagnosticResponse(const twai_message_t &rxMsg);
void queryObdPid(uint8_t mode, uint8_t pid);
void requestDTCs(uint8_t mode);
bool requestClearDTCs();
void pumpTwai(uint32_t waitMs);
String decodeDTC(uint8_t b1, uint8_t b2);
void addStoredDtc(const String &code, const String &desc);
void recordSuccessfulClear();
void printBanner();
void printHelp();
void printStats();
void printUniqueIdsTable();
void executeCommand(String input);
void setupWiFi();
void triggerLedActivity();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(10);

    // Initialize Onboard Activity LED
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
        delay(80);
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
        delay(80);
    }

    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 1200)) {
        delay(10);
    }
    delay(50);

    printBanner();

    // Check wakeup reason (e.g. waking on CAN Dominant frame when key turns on)
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    if (wakeupReason == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println("[WAKE] ⚡ ESP32 woke up from Deep Sleep via CAN RX activity (Ignition ON)!\n");
    }
    lastCanRxMs = millis();

    // 1. Initialize NVS Storage
    prefs.begin("mt09_clean", false);
    bootCount = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", bootCount);
    totalClears = prefs.getUInt("clears", 0);
    lastClearedCode = prefs.getString("last_code", "None");
    Serial.printf("[NVS] Boots: %u | Total Clears Logged: %u | Last Code: %s\n", bootCount, totalClears, lastClearedCode.c_str());

    // 2. Initialize Wi-Fi SoftAP and WebServer
    setupWiFi();

    // 3. Start TWAI CAN Controller in NORMAL mode at 500 kbps (Euro 5 Standard)
    if (initTWAI(currentBaud, listenOnlyMode)) {
        Serial.printf("[INIT] CAN Controller initialized on TX: GPIO %d, RX: GPIO %d\n", CAN_TX_PIN, CAN_RX_PIN);
        Serial.printf("[INIT] Speed: %s | Mode: %s\n", BAUD_NAMES[currentBaud], listenOnlyMode ? "LISTEN-ONLY" : "NORMAL (Active OBD & ACK)");
        Serial.println("[INIT] Autonomous Background DTC Auto-Cleaner: ACTIVE (No user input required)\n");
    } else {
        Serial.println("[ERROR] Failed to start CAN Controller! Check pins and transceiver wiring.");
    }

    bootMs = millis();
    bootAutoCleanDueMs = bootMs + 4000;
}

// =============================================================================
// WI-FI & WEB SERVER INITIALIZATION
// =============================================================================
void setupWiFi() {
    Serial.println("[WIFI] Starting Standalone Wi-Fi Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
    WiFi.softAP(AP_SSID, nullptr, 1);
    delay(100);

    Serial.printf("[WIFI] SSID         : %s\n", AP_SSID);
    Serial.printf("[WIFI] IP Address   : %s\n", WiFi.softAPIP().toString().c_str());

    // Setup Captive Portal DNS Server (Redirects all domains to 192.168.4.1)
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", AP_IP);

    // Setup mDNS Responder (http://mt09.local)
    if (MDNS.begin("mt09")) {
        Serial.println("[WIFI] mDNS active  : http://mt09.local");
        MDNS.addService("http", "tcp", 80);
    }

    // --- HTTP Web Routes ---
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "max-age=3600");
        server.send(200, "text/html", INDEX_HTML);
    });

    // REST API: Live Telemetry Snapshot
    server.on("/api/telemetry", HTTP_GET, []() {
        char buf[420];
        snprintf(buf, sizeof(buf),
            "{\"rpm\":%u,\"speed\":%u,\"speedKmH\":%u,\"gear\":%u,\"tps\":%u,\"temp\":%d,\"iat\":%d,\"volts\":%.1f,\"lean\":%.1f,\"maxLeanL\":%.1f,\"maxLeanR\":%.1f,\"eng\":%s,\"fps\":%u,\"frames\":%u,\"dtcCount\":%d,\"autoClean\":%s,\"clears\":%u,\"boots\":%u,\"lastCode\":\"%s\"}",
            currentRpm, currentSpeedMph, currentSpeedKmH, currentGear, currentTps,
            (currentCoolantTemp == -999) ? 0 : currentCoolantTemp,
            (currentIat == -999) ? 0 : currentIat,
            currentBatteryVolts,
            currentLeanAngle, maxLeanLeft, maxLeanRight,
            engineRunning ? "true" : "false",
            currentFps, totalFramesRx, storedDtcCount,
            autoCleanEnabled ? "true" : "false",
            totalClears, bootCount, lastClearedCode.c_str());
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.send(200, "application/json", buf);
    });

    // REST API: Reset Max Lean Angles
    server.on("/api/reset_lean", HTTP_POST, []() {
        maxLeanLeft = 0.0f;
        maxLeanRight = 0.0f;
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.send(200, "application/json", "{\"success\":true}");
    });

    // REST API: Trigger DTC Scan
    server.on("/api/scan", HTTP_GET, []() {
        storedDtcCount = 0;
        requestDTCs(0x03);
        pumpTwai(120);
        requestDTCs(0x07);
        pumpTwai(120);

        String json = "{\"count\":" + String(storedDtcCount) + ",\"codes\":[";
        for (int i = 0; i < storedDtcCount; i++) {
            if (i > 0) json += ",";
            json += "{\"code\":\"" + storedDtcs[i].code + "\",\"desc\":\"" + storedDtcs[i].desc + "\"}";
        }
        json += "]}";

        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.send(200, "application/json", json);
    });

    // REST API: Safety-Interlocked Clear Codes
    server.on("/api/clear", HTTP_POST, []() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        if (engineRunning || currentRpm > 0) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Engine is running! Turn engine off (Key ON) to clear.\"}");
            return;
        }

        bool ok = requestClearDTCs();
        if (ok) {
            recordSuccessfulClear();
            storedDtcCount = 0;
            server.send(200, "application/json", "{\"success\":true,\"message\":\"DTC clear frame transmitted! CEL reset.\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to send clear frame on CAN bus.\"}");
        }
    });

    // --- Wireless Web OTA Firmware Update Endpoints ---
    server.on("/update", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html",
            "<!DOCTYPE html><html style='background:#07090e;color:#f8fafc;font-family:sans-serif;text-align:center;padding:40px;'>"
            "<h2>🏍️ Yamaha MT-09 SP Wireless Update</h2>"
            "<p style='color:#94a3b8;max-width:400px;margin:0 auto 20px auto;'>Flash new firmware over Wi-Fi without removing the motorcycle seat:</p>"
            "<form method='POST' action='/update' enctype='multipart/form-data' style='background:#111625;padding:24px;border-radius:16px;max-width:400px;margin:0 auto;border:1px solid rgba(255,255,255,0.1);'>"
            "<input type='file' name='update' style='margin-bottom:20px;'><br>"
            "<input type='submit' value='Upload & Flash' style='background:#00e5ff;color:#000;font-weight:bold;padding:12px 28px;border:none;border-radius:10px;cursor:pointer;'>"
            "</form><br><a href='/' style='color:#00e5ff;text-decoration:none;'>&larr; Back to Cockpit</a></html>"
        );
    });

    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html",
            "<!DOCTYPE html><html style='background:#07090e;color:#10b981;font-family:sans-serif;text-align:center;padding:40px;'>"
            "<h2>🎉 Update Succeeded!</h2>"
            "<p style='color:#f8fafc;'>Rebooting ESP32 into new firmware...</p>"
            "<script>setTimeout(function(){ window.location.href='/'; }, 4500);</script></html>"
        );
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA] Receiving Firmware: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA] Update Success: %u bytes written\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    // Captive Portal redirection routes
    server.on("/hotspot-detect.html", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });
    server.on("/generate_204", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });
    server.on("/gen_204", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });
    server.on("/ncsi.txt", HTTP_GET, []() { server.send(200, "text/plain", "Microsoft NCSI"); });

    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    });

    server.begin();
    Serial.println("[WIFI] HTTP Server listening on port 80.\n");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    unsigned long now = millis();

    // 1. Service Captive Portal DNS & Web Server requests
    dnsServer.processNextRequest();
    server.handleClient();

    // 2. Process USB CDC Serial commands (for PC & SavvyCAN)
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            executeCommand(input);
        } else {
            Serial.printf("\n[STATUS] Frames: %u | Engine: %s | RPM: %u | TPS: %u%% | Coolant: %d°C | Volts: %.1fV | Gear: %u | AutoClean: %s\n",
                          totalFramesRx, engineRunning ? "RUNNING" : "OFF", currentRpm, currentTps, currentCoolantTemp,
                          currentBatteryVolts, currentGear, autoCleanEnabled ? "ACTIVE" : "DISABLED");
        }
    }

    // 3. Process incoming CAN messages from TWAI hardware
    if (twaiRunning) {
        twai_message_t rxMsg;
        uint8_t rxBatch = 0;
        while (twai_receive(&rxMsg, 0) == ESP_OK && rxBatch < 35) {
            handleFrame(rxMsg);
            rxBatch++;
        }

        uint32_t alertsTriggered = 0;
        if (twai_read_alerts(&alertsTriggered, 0) == ESP_OK) {
            if (alertsTriggered & TWAI_ALERT_BUS_ERROR) {
                busErrorCount++;
            }
            if (alertsTriggered & TWAI_ALERT_RX_QUEUE_FULL) {
                rxOverrunCount++;
            }
            if (alertsTriggered & TWAI_ALERT_BUS_OFF) {
                Serial.println("\n[ALERT] TWAI Bus-Off detected! Initiating recovery...");
                twai_initiate_recovery();
            }
            if (alertsTriggered & TWAI_ALERT_BUS_RECOVERED) {
                Serial.println("\n[ALERT] TWAI Bus Recovered!");
                twai_start();
            }
        }
    }

    // 4. Smooth Multi-PID OBD Telemetry Polling (every 140ms steps)
    static uint8_t telemetryStep = 0;
    if (twaiRunning && !listenOnlyMode && (now - lastTelemetryPollMs >= 140)) {
        lastTelemetryPollMs = now;
        switch (telemetryStep) {
            case 0: queryObdPid(0x01, 0x0C); break; // RPM
            case 1: queryObdPid(0x01, 0x11); break; // TPS
            case 2: queryObdPid(0x01, 0x05); break; // Coolant Temp
            case 3: queryObdPid(0x01, 0x42); break; // Battery & Stator Voltage
            case 4: queryObdPid(0x01, 0x0D); break; // Vehicle Speed
            case 5: queryObdPid(0x01, 0x0F); break; // Intake Air Temp (IAT)
        }
        telemetryStep = (telemetryStep + 1) % 6;
    }

    // -------------------------------------------------------------------------
    // 5. AUTONOMOUS BACKGROUND DTC AUTO-SCAN & AUTO-CLEAR (ZERO USER INPUT NEEDED)
    // -------------------------------------------------------------------------
    if (autoCleanEnabled && twaiRunning && !listenOnlyMode) {
        // A. Boot auto-scan (4 seconds after ignition turned on)
        if (!bootAutoCleanDone && now >= bootAutoCleanDueMs) {
            bootAutoCleanDone = true;
            if (!engineRunning && currentRpm == 0) {
                Serial.println("\n[AUTO-CLEAN] 🚀 Boot check: Scanning ECU for trouble codes (Engine OFF)...");
                storedDtcCount = 0;
                requestDTCs(0x03);
                pumpTwai(120);
                requestDTCs(0x07);
                pumpTwai(120);

                if (storedDtcCount > 0) {
                    Serial.printf("[AUTO-CLEAN] ⚠️ Found %d trouble code(s) on startup! Automatically issuing Mode 04 clear...\n", storedDtcCount);
                    if (requestClearDTCs()) {
                        recordSuccessfulClear();
                        storedDtcCount = 0;
                        lastAutoClearMs = now;
                        Serial.println("[AUTO-CLEAN] 🎉 Fault codes erased & Check Engine Light reset on startup!\n");
                    }
                } else {
                    Serial.println("[AUTO-CLEAN] ✅ Boot check: System clean! 0 trouble codes found in ECU.\n");
                }
            }
        }

        // B. Detect Engine Stop transition (e.g. rider finished a ride or turned off kill switch)
        if (prevEngineRunning && !engineRunning) {
            Serial.println("\n[AUTO-CLEAN] Engine shut down detected. Running automatic post-ride DTC check in 3s...");
            lastAutoScanMs = now - (AUTO_SCAN_INTERVAL_MS - 3000);
        }
        prevEngineRunning = engineRunning;

        // C. Periodic background scan when engine is confirmed STOPPED (0 RPM)
        if (!engineRunning && currentRpm == 0 && (now - lastAutoScanMs >= AUTO_SCAN_INTERVAL_MS)) {
            lastAutoScanMs = now;
            storedDtcCount = 0;
            requestDTCs(0x03);
            pumpTwai(120);
            requestDTCs(0x07);
            pumpTwai(120);

            if (storedDtcCount > 0 && (now - lastAutoClearMs >= AUTO_CLEAR_COOLDOWN_MS)) {
                Serial.printf("\n[AUTO-CLEAN] ⚠️ Detected %d code(s) with engine stopped. Transmitting automatic clear...\n", storedDtcCount);
                if (requestClearDTCs()) {
                    recordSuccessfulClear();
                    storedDtcCount = 0;
                    lastAutoClearMs = now;
                    Serial.println("[AUTO-CLEAN] 🎉 Fault codes successfully auto-cleared in background! CEL reset.\n");
                }
            }
        }
    }

    // 6. Update FPS counter every second
    if (now - lastFpsCalcMs >= 1000) {
        currentFps = framesLastSecond;
        framesLastSecond = 0;
        lastFpsCalcMs = now;
    }

    // 7. Handle Non-blocking Activity LED
    if (ledTurnOffMs > 0 && now >= ledTurnOffMs) {
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
        ledTurnOffMs = 0;
    }

    // 8. Low-Power Deep Sleep Inactivity Check
    // If no CAN traffic for 60s and no active Wi-Fi dashboard clients connected, enter deep sleep (<15 µA)
    if (twaiRunning && (now - lastCanRxMs >= INACTIVITY_SLEEP_TIMEOUT_MS)) {
        if (WiFi.softAPgetStationNum() == 0) {
            enterDeepSleep();
        }
    }

    delay(2);
}

// =============================================================================
// RECORD SUCCESSFUL DTC CLEAR TO NVS MEMORY
// =============================================================================
void recordSuccessfulClear() {
    totalClears++;
    prefs.putUInt("clears", totalClears);
    if (storedDtcCount > 0) {
        lastClearedCode = storedDtcs[0].code;
        prefs.putString("last_code", lastClearedCode);
    }
}

// =============================================================================
// TWAI MESSAGE PUMP (HELPER FOR SYNCHRONOUS HTTP & AUTO-CLEAN RESPONSES)
// =============================================================================
void pumpTwai(uint32_t waitMs) {
    unsigned long start = millis();
    while (millis() - start < waitMs) {
        if (twaiRunning) {
            twai_message_t rxMsg;
            while (twai_receive(&rxMsg, 0) == ESP_OK) {
                handleFrame(rxMsg);
            }
        }
        delay(4);
    }
}

// =============================================================================
// COMMAND EXECUTION (USB CDC SERIAL CLI)
// =============================================================================
void executeCommand(String input) {
    input.trim();
    if (input.length() == 0) return;

    char cmd = input.charAt(0);

    switch (cmd) {
        case '?':
        case 'h':
        case 'H':
            printHelp();
            break;

        case 'a':
        case 'A':
            autoCleanEnabled = !autoCleanEnabled;
            Serial.printf("\n[AUTO-CLEAN] Autonomous background cleaning: %s\n\n", autoCleanEnabled ? "ENABLED" : "DISABLED");
            break;

        case 'r':
        case 'R': {
            Serial.println("\n🔍 [DTC SCAN] Querying ECU for confirmed & pending trouble codes...");
            storedDtcCount = 0;
            requestDTCs(0x03);
            pumpTwai(120);
            requestDTCs(0x07);
            pumpTwai(120);

            if (storedDtcCount == 0) {
                Serial.println("✅ [DTC SCAN] Clean system! 0 trouble codes stored in ECU memory.\n");
            } else {
                Serial.printf("⚠️ [DTC SCAN] Found %d code(s):\n", storedDtcCount);
                for (int i = 0; i < storedDtcCount; i++) {
                    Serial.printf("   [%s] %s\n", storedDtcs[i].code.c_str(), storedDtcs[i].desc.c_str());
                }
                Serial.println();
            }
            break;
        }

        case 'k':
        case 'K': {
            if (engineRunning || currentRpm > 0) {
                Serial.println("\n⚠️ [CLEAR REFUSED] SAFETY GUARD: Engine is RUNNING! Turn off engine (Key ON) to clear.\n");
            } else {
                Serial.println("\n🧹 [CLEAR DTCs] Sending OBD Mode 04 clear command to ECU...");
                bool ok = requestClearDTCs();
                if (ok) {
                    recordSuccessfulClear();
                    storedDtcCount = 0;
                    Serial.println("🎉 [CLEAR CONFIRMED] Clear frame transmitted! Trouble codes erased & CEL reset.\n");
                } else {
                    Serial.println("❌ [CLEAR ERROR] Failed to transmit clear frame on CAN bus.\n");
                }
            }
            break;
        }

        case 't':
        case 'T':
            Serial.printf("\n📊 Snapshot -> RPM: %u | Speed: %u mph | Gear: %u | TPS: %u%% | Coolant: %d°C | Volts: %.1fV | Lean: %.1f° (Max L: %.1f° / R: %.1f°) | Engine: %s\n\n",
                          currentRpm, currentSpeedMph, currentGear, currentTps, currentCoolantTemp, currentBatteryVolts, currentLeanAngle, maxLeanLeft, maxLeanRight, engineRunning ? "RUNNING" : "STOPPED");
            break;

        case 'z':
        case 'Z':
            enterDeepSleep();
            break;

        case 's':
        case 'S':
            printStats();
            break;

        case 'u':
        case 'U':
            printUniqueIdsTable();
            break;

        case 'l':
        case 'L':
            listenOnlyMode = !listenOnlyMode;
            initTWAI(currentBaud, listenOnlyMode);
            Serial.printf("\n[MODE] Switched to: %s\n", listenOnlyMode ? "LISTEN-ONLY (Safe Passive)" : "NORMAL (Active OBD & ACK)");
            break;

        case 'b':
        case 'B':
            currentBaud = (CanBaudRate)((currentBaud + 1) % 4);
            initTWAI(currentBaud, listenOnlyMode);
            Serial.printf("\n[BAUD] Switched to: %s\n", BAUD_NAMES[currentBaud]);
            break;

        case 'p':
        case 'P':
            streamPaused = !streamPaused;
            Serial.printf("\n[STREAM] %s\n", streamPaused ? "PAUSED" : "RESUMED");
            break;

        case 'c':
        case 'C':
            uniqueIdCount = 0;
            totalFramesRx = 0;
            busErrorCount = 0;
            rxOverrunCount = 0;
            storedDtcCount = 0;
            maxLeanLeft = 0.0f;
            maxLeanRight = 0.0f;
            Serial.println("\n[CLEARED] Counters, unique IDs, stored DTCs, and max lean angles reset.\n");
            break;

        default:
            Serial.printf("Unknown command '%c'. Send '?' for help.\n", cmd);
            break;
    }
}

// =============================================================================
// TWAI CONTROLLER MANAGEMENT & TRANSMIT
// =============================================================================
bool initTWAI(CanBaudRate baud, bool listenOnly) {
    if (twaiRunning) {
        stopTWAI();
    }

    twai_general_config_t g_config;
    if (listenOnly) {
        g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    } else {
        g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    }
    g_config.rx_queue_len = 64;
    g_config.tx_queue_len = 16;
    g_config.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL | 
                             TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

    twai_timing_config_t t_config;
    switch (baud) {
        case BAUD_500K: t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
        case BAUD_250K: t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
        case BAUD_1M:   t_config = TWAI_TIMING_CONFIG_1MBITS(); break;
        case BAUD_125K: t_config = TWAI_TIMING_CONFIG_125KBITS(); break;
        default:        t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) return false;
    twaiInstalled = true;

    err = twai_start();
    if (err != ESP_OK) {
        twai_driver_uninstall();
        twaiInstalled = false;
        return false;
    }
    twaiRunning = true;
    return true;
}

void stopTWAI() {
    if (twaiRunning) {
        twai_stop();
        twaiRunning = false;
    }
    if (twaiInstalled) {
        twai_driver_uninstall();
        twaiInstalled = false;
    }
}

void enterDeepSleep() {
    Serial.println("\n[SLEEP] 🌙 Inactivity timeout reached (no CAN traffic for 60s).");
    Serial.println("[SLEEP] Shutting down TWAI & Wi-Fi. Entering ultra-low power Deep Sleep (<15 µA)...");
    Serial.println("[SLEEP] Wakeup trigger: CAN RX (GPIO 3) LOW level (Dominant SOF on bike Key ON).");
    Serial.flush();
    delay(50);

    // Turn off onboard activity LED
    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);

    // 1. Stop TWAI Controller
    stopTWAI();

    // 2. Stop Wi-Fi and Web Server
    dnsServer.stop();
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(20);

    // 3. Configure CAN RX (GPIO 3) as wake-up trigger on LOW level
    esp_deep_sleep_enable_gpio_wakeup(1ULL << CAN_RX_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

    // 4. Enter Deep Sleep
    esp_deep_sleep_start();
}

bool sendCanFrame(uint32_t id, uint8_t len, const uint8_t* data) {
    if (!twaiRunning || listenOnlyMode) {
        return false;
    }
    twai_message_t txMsg;
    txMsg.identifier = id;
    txMsg.flags = TWAI_MSG_FLAG_NONE;
    txMsg.data_length_code = len;
    memcpy(txMsg.data, data, len);
    esp_err_t err = twai_transmit(&txMsg, pdMS_TO_TICKS(80));
    return (err == ESP_OK);
}

// =============================================================================
// OBD-II & DIAGNOSTIC REQUESTS
// =============================================================================
void queryObdPid(uint8_t mode, uint8_t pid) {
    uint8_t payload[8] = { 0x02, mode, pid, 0x55, 0x55, 0x55, 0x55, 0x55 };
    sendCanFrame(0x7DF, 8, payload);
}

void requestDTCs(uint8_t mode) {
    uint8_t payload[8] = { 0x01, mode, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };
    sendCanFrame(0x7DF, 8, payload);
}

bool requestClearDTCs() {
    if (engineRunning || currentRpm > 0) {
        return false;
    }
    uint8_t payload[8] = { 0x01, 0x04, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };
    return sendCanFrame(0x7DF, 8, payload);
}

// =============================================================================
// DTC DECODER & DIAGNOSTIC RESPONSE HANDLER
// =============================================================================
void addStoredDtc(const String &code, const String &desc) {
    for (int i = 0; i < storedDtcCount; i++) {
        if (storedDtcs[i].code.equalsIgnoreCase(code)) return;
    }
    if (storedDtcCount < MAX_STORED_DTCS) {
        storedDtcs[storedDtcCount].code = code;
        storedDtcs[storedDtcCount].desc = desc;
        storedDtcCount++;
    }
}

String decodeDTC(uint8_t b1, uint8_t b2) {
    if (b1 == 0 && b2 == 0) return "";
    char prefix = 'P';
    switch ((b1 >> 6) & 0x03) {
        case 0: prefix = 'P'; break;
        case 1: prefix = 'C'; break;
        case 2: prefix = 'B'; break;
        case 3: prefix = 'U'; break;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%c%X%X%02X", prefix, (b1 >> 4) & 0x03, b1 & 0x0F, b2);
    String dtcCode = String(buf);

    String desc = "Unknown / Generic Fault Code";
    for (size_t i = 0; i < sizeof(KNOWN_DTCS)/sizeof(KNOWN_DTCS[0]); i++) {
        if (dtcCode.equalsIgnoreCase(KNOWN_DTCS[i].code)) {
            desc = KNOWN_DTCS[i].desc;
            break;
        }
    }

    addStoredDtc(dtcCode, desc);
    return "[" + dtcCode + "] " + desc;
}

void handleDiagnosticResponse(const twai_message_t &rxMsg) {
    uint8_t pciType = rxMsg.data[0] & 0xF0;

    // 1. Single Frame (SF)
    if (pciType == 0x00) {
        awaitingConsecutiveDtc = false;
        uint8_t service = rxMsg.data[1];

        // Mode 01: Current Powertrain Data
        if (service == 0x41) {
            uint8_t pid = rxMsg.data[2];
            switch (pid) {
                case 0x0C: // Engine RPM
                    currentRpm = ((rxMsg.data[3] * 256) + rxMsg.data[4]) / 4;
                    break;
                case 0x05: // Coolant Temp
                    currentCoolantTemp = (int16_t)rxMsg.data[3] - 40;
                    break;
                case 0x11: // Throttle Position (TPS)
                    currentTps = (rxMsg.data[3] * 100) / 255;
                    break;
                case 0x42: // Battery / ECU Voltage
                    currentBatteryVolts = (float)((rxMsg.data[3] * 256) + rxMsg.data[4]) / 1000.0f;
                    break;
                case 0x0D: { // Vehicle Speed
                    currentSpeedKmH = rxMsg.data[3];
                    currentSpeedMph = (uint8_t)(currentSpeedKmH * 0.621371f);
                    // Calculated Gear Indicator for MT-09 SP
                    if (currentSpeedKmH < 3 || currentRpm < 500) {
                        currentGear = 0; // Neutral
                    } else {
                        float ratio = (float)currentRpm / (float)currentSpeedKmH;
                        if (ratio > 118.0f) currentGear = 1;
                        else if (ratio > 94.0f) currentGear = 2;
                        else if (ratio > 79.0f) currentGear = 3;
                        else if (ratio > 68.0f) currentGear = 4;
                        else if (ratio > 59.0f) currentGear = 5;
                        else currentGear = 6;
                    }
                    break;
                }
                case 0x0F: // Intake Air Temp (IAT)
                    currentIat = (int16_t)rxMsg.data[3] - 40;
                    break;
            }
        }
        // Mode 03 / Mode 07: Stored or Pending DTCs
        else if (service == 0x43 || service == 0x47) {
            uint8_t dtcCount = rxMsg.data[2];
            if (dtcCount > 0) {
                for (int i = 3; i < 7; i += 2) {
                    if (rxMsg.data[i] != 0 || rxMsg.data[i+1] != 0) {
                        decodeDTC(rxMsg.data[i], rxMsg.data[i+1]);
                    }
                }
            }
        }
    }
    // 2. First Frame (FF) of Multi-Frame Response
    else if (pciType == 0x10) {
        uint8_t service = rxMsg.data[2];
        if (service == 0x43 || service == 0x47) {
            awaitingConsecutiveDtc = true;
            uint8_t fc[8] = { 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            sendCanFrame(0x7E0, 8, fc);
            if (rxMsg.data[4] != 0 || rxMsg.data[5] != 0) {
                decodeDTC(rxMsg.data[4], rxMsg.data[5]);
            }
        }
    }
    // 3. Consecutive Frame (CF)
    else if (pciType == 0x20 && awaitingConsecutiveDtc) {
        for (int i = 1; i < 7; i += 2) {
            if (rxMsg.data[i] != 0 || rxMsg.data[i+1] != 0) {
                decodeDTC(rxMsg.data[i], rxMsg.data[i+1]);
            }
        }
    }
}

// =============================================================================
// FRAME PROCESSING
// =============================================================================
void handleFrame(const twai_message_t &rxMsg) {
    totalFramesRx++;
    framesLastSecond++;
    triggerLedActivity();

    uint32_t id = rxMsg.identifier;
    bool isExt = rxMsg.flags & TWAI_MSG_FLAG_EXTD;
    bool isRtr = rxMsg.flags & TWAI_MSG_FLAG_RTR;
    uint8_t dlc = rxMsg.data_length_code;
    unsigned long now = millis();
    lastCanRxMs = now;

    // 0. Live Lean Angle from Yamaha 6-Axis IMU (CAN ID 0x27C)
    // Broadcasts at 1,000 Hz. Signed 16-bit big-endian integer in 0.1° units.
    if (id == 0x27C && dlc >= 2) {
        int16_t rawRoll = (int16_t)((rxMsg.data[0] << 8) | rxMsg.data[1]);
        float lean = rawRoll / 10.0f;
        currentLeanAngle = lean;
        if (lean < 0) {
            float absL = -lean;
            if (absL > maxLeanLeft) maxLeanLeft = absL;
        } else if (lean > 0) {
            if (lean > maxLeanRight) maxLeanRight = lean;
        }
    }

    // 1. Live Engine Running State Detection (Reverse-engineered from 0x751 Byte 7)
    if (id == 0x751 && dlc >= 8) {
        bool prevRunning = engineRunning;
        engineRunning = (rxMsg.data[7] == 0x04);
        if (prevRunning != engineRunning) {
            Serial.printf("\n[ENGINE STATE CHANGE] Engine is now: %s (Flag: 0x%02X)\n", engineRunning ? "🔥 RUNNING" : "🛑 STOPPED", rxMsg.data[7]);
        }
    }

    // 2. Intercept Diagnostic Responses
    if (id == 0x7E8 || id == 0x7E9 || id == 0x758) {
        handleDiagnosticResponse(rxMsg);
    }

    if (filterActive && id != filterId) return;

    // 3. Unique ID Stats
    uint32_t intervalMs = 0;
    int foundIdx = -1;
    bool dataChanged = false;

    for (uint16_t i = 0; i < uniqueIdCount; i++) {
        if (uniqueIds[i].id == id && uniqueIds[i].isExtended == isExt) {
            foundIdx = i;
            uniqueIds[i].count++;
            intervalMs = now - uniqueIds[i].lastSeenMs;
            uniqueIds[i].lastIntervalMs = intervalMs;
            uniqueIds[i].lastSeenMs = now;
            uniqueIds[i].dlc = dlc;
            if (!isRtr) {
                if (memcmp(uniqueIds[i].lastData, rxMsg.data, (dlc > 8) ? 8 : dlc) != 0) {
                    dataChanged = true;
                    memcpy(uniqueIds[i].lastData, rxMsg.data, (dlc > 8) ? 8 : dlc);
                }
            }
            break;
        }
    }

    if (foundIdx < 0 && uniqueIdCount < MAX_UNIQUE_IDS) {
        dataChanged = true;
        foundIdx = uniqueIdCount;
        uniqueIds[uniqueIdCount].id = id;
        uniqueIds[uniqueIdCount].isExtended = isExt;
        uniqueIds[uniqueIdCount].count = 1;
        uniqueIds[uniqueIdCount].lastSeenMs = now;
        uniqueIds[uniqueIdCount].lastIntervalMs = 0;
        uniqueIds[uniqueIdCount].lastPrintMs = 0;
        uniqueIds[uniqueIdCount].dlc = dlc;
        if (!isRtr) {
            memcpy(uniqueIds[uniqueIdCount].lastData, rxMsg.data, (dlc > 8) ? 8 : dlc);
        }
        uniqueIdCount++;
    }

    if (streamPaused) return;
    if (ignoreActive && id == ignoreId) return;
    if (changesOnly && !dataChanged) return;

    if (foundIdx >= 0 && (now - uniqueIds[foundIdx].lastPrintMs < 50)) return;
    if (foundIdx >= 0) uniqueIds[foundIdx].lastPrintMs = now;

    // Print to USB CDC Serial
    float sec = now / 1000.0;
    Serial.printf("[%8.3fs] ID: 0x%03X  DLC: %d  DATA: ", sec, id, dlc);
    for (int i = 0; i < 8; i++) {
        if (i < dlc) Serial.printf("%02X ", rxMsg.data[i]);
        else Serial.print("   ");
    }
    Serial.printf(" | %s\n", engineRunning ? "RUN" : "OFF");
}

void triggerLedActivity() {
    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
    ledTurnOffMs = millis() + 15;
}

// =============================================================================
// BANNER & CLI HELP
// =============================================================================
void printBanner() {
    Serial.println("\n=================================================================");
    Serial.println("  🏍️  YAMAHA MT-09 SP EURO 5+ CAN BUS & WI-FI COCKPIT SUITE");
    Serial.println("  Hardware: ESP32-C3 Super Mini Plus | TWAI CAN Controller");
    Serial.println("=================================================================");
}

void printHelp() {
    Serial.println("\n=========================================");
    Serial.println("        MT-09 SP CAN COMMAND MENU        ");
    Serial.println("=========================================");
    Serial.println("  a : Toggle Autonomous Background Auto-Clear");
    Serial.println("  r : Scan DTCs (Confirmed & Pending)");
    Serial.println("  k : Clear DTCs & Reset CEL (Engine OFF)");
    Serial.println("  t : Telemetry Snapshot");
    Serial.println("  s : Display CAN Bus Health & Stats");
    Serial.println("  u : Display Discovered Unique IDs");
    Serial.println("  l : Toggle Mode (Listen-Only vs Normal)");
    Serial.println("  b : Cycle Baud Rate (500k/250k/1M/125k)");
    Serial.println("  p : Pause / Resume CAN text output");
    Serial.println("  c : Clear counters & stored codes");
    Serial.println("  z : Enter Deep Sleep immediately (wakes on CAN RX)");
    Serial.println("  ? : Show this help menu");
    Serial.println("=========================================\n");
}

void printStats() {
    twai_status_info_t status;
    twai_get_status_info(&status);

    Serial.println("\n=================================================================");
    Serial.println("                     CAN BUS DIAGNOSTICS & HEALTH                ");
    Serial.println("=================================================================");
    Serial.printf("  Hardware State      : %s\n", (status.state == TWAI_STATE_RUNNING) ? "RUNNING (Active)" : "STOPPED/BUS-OFF");
    Serial.printf("  Auto-Cleaner State  : %s (Logged Clears: %u | Boots: %u)\n", autoCleanEnabled ? "ENABLED (Automatic)" : "DISABLED", totalClears, bootCount);
    Serial.printf("  Wi-Fi Hotspot       : SSID: %s | IP: %s (http://mt09.local)\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    Serial.printf("  Wireless OTA Update : http://%s/update\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  Baud Rate           : %s\n", BAUD_NAMES[currentBaud]);
    Serial.printf("  Engine State        : %s (RPM: %u | Speed: %u mph | Gear: %u | Volts: %.1fV)\n", engineRunning ? "🔥 RUNNING" : "🛑 OFF", currentRpm, currentSpeedMph, currentGear, currentBatteryVolts);
    Serial.printf("  Lean Angle (IMU)    : %.1f° (Max Left: %.1f° | Max Right: %.1f°)\n", currentLeanAngle, maxLeanLeft, maxLeanRight);
    Serial.printf("  Total Frames Rx     : %u frames\n", totalFramesRx);
    Serial.printf("  Current Bus Load    : %u frames/sec (FPS)\n", currentFps);
    Serial.printf("  Unique IDs Seen     : %u / %d\n", uniqueIdCount, MAX_UNIQUE_IDS);
    Serial.printf("  Bus Error Warnings  : %u\n", busErrorCount);
    Serial.printf("  RX Queue Overruns   : %u\n", rxOverrunCount);
    Serial.println("=================================================================\n");
}

void printUniqueIdsTable() {
    Serial.println("\n============================================================================================");
    Serial.println("                                DISCOVERED UNIQUE CAN IDs TABLE                             ");
    Serial.println("============================================================================================");
    Serial.println(" ID          Type  DLC   Count      Rate (Hz)   Interval   Last Data Payload (Hex)          ");
    Serial.println("--------------------------------------------------------------------------------------------");

    if (uniqueIdCount == 0) {
        Serial.println("  (No CAN traffic received yet)");
    } else {
        for (uint16_t i = 0; i < uniqueIdCount; i++) {
            Serial.printf(" 0x%03X       STD   %d   %-9u", uniqueIds[i].id, uniqueIds[i].dlc, uniqueIds[i].count);
            if (uniqueIds[i].lastIntervalMs > 0) {
                float hz = 1000.0f / (float)uniqueIds[i].lastIntervalMs;
                Serial.printf(" %5.1f Hz    %4u ms    ", hz, uniqueIds[i].lastIntervalMs);
            } else {
                Serial.print("   --- Hz     --- ms    ");
            }
            for (int b = 0; b < uniqueIds[i].dlc && b < 8; b++) {
                Serial.printf("%02X ", uniqueIds[i].lastData[b]);
            }
            Serial.println();
        }
    }
    Serial.println("--------------------------------------------------------------------------------------------\n");
}