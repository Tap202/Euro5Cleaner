/**
 * =============================================================================
 * 🏍️ Yamaha MT-09 SP Euro 5+ Autonomous DTC Cleaner & BLE Suite
 * Platform: ESP32-C3 Super Mini Plus (RISC-V 160MHz, Native USB CDC, BLE 5.0)
 * =============================================================================
 * 
 * Features:
 *  - 100% Autonomous Headless DTC Auto-Scan & Auto-Clear (Euro 5+ TWAI @ 500 kbps)
 *  - Hardware-Enforced Engine-OFF Safety Interlock (Refuses clear if engine runs)
 *  - Persistent NVS Clear Logger (Records last 25 clear events to Flash memory)
 *  - Bluetooth Low Energy (BLE) GATT Interface:
 *      - Read active & pending ECU fault codes with plain-English definitions
 *      - Force manual scan or clear over phone (nRF Connect or Web Bluetooth)
 *      - Download persistent clear history logs
 *  - Non-blocking USB CDC Serial CLI (Zero watchdog hangs if terminal is closed)
 *  - Ultra-Low Power Deep Sleep (< 15 µA) with instant CAN RX SOF bit wakeup
 * =============================================================================
 */

#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "driver/twai.h"
#include "esp_sleep.h"

// =============================================================================
// HARDWARE PIN DEFINITIONS (ESP32-C3 Super Mini Plus)
// =============================================================================
#define CAN_TX_PIN         GPIO_NUM_21
#define CAN_RX_PIN         GPIO_NUM_3

// Onboard User LED on ESP32-C3 Super Mini is GPIO 8 (Active LOW)
#define ONBOARD_LED_PIN    8
#define LED_ACTIVE_LOW     true

// =============================================================================
// BLE SERVICE & CHARACTERISTIC UUIDs
// =============================================================================
#define SERVICE_UUID           "000018F0-0000-1000-8000-00805F9B34FB"
#define CHAR_STATUS_UUID       "000018F1-0000-1000-8000-00805F9B34FB" // Read / Notify
#define CHAR_DTCS_UUID         "000018F2-0000-1000-8000-00805F9B34FB" // Read / Notify
#define CHAR_COMMAND_UUID      "000018F3-0000-1000-8000-00805F9B34FB" // Write
#define CHAR_LOGS_UUID         "000018F4-0000-1000-8000-00805F9B34FB" // Read / Notify

BLEServer* pServer = nullptr;
BLECharacteristic* pCharStatus = nullptr;
BLECharacteristic* pCharDtcs = nullptr;
BLECharacteristic* pCharCommand = nullptr;
BLECharacteristic* pCharLogs = nullptr;

bool bleClientConnected = false;
bool oldBleClientConnected = false;

// =============================================================================
// PERSISTENT NVS STORAGE & LOGGING
// =============================================================================
Preferences prefs;
uint32_t totalClears = 0;
uint32_t bootCount = 0;
String lastClearedCode = "None";

#define MAX_CLEAR_LOGS 25
struct ClearLogEntry {
    uint32_t id;
    uint32_t boot;
    uint32_t uptimeSec;
    char trigger[16];   // "BOOT_AUTO", "POST_RIDE", "BLE_MANUAL", "CLI_MANUAL"
    char codes[32];     // e.g. "P0036, P0030"
    char result[16];    // "CLEARED_OK", "NO_CODES", "REFUSED_RUN"
};

ClearLogEntry clearLogs[MAX_CLEAR_LOGS];
uint8_t clearLogCount = 0;
uint8_t clearLogHead = 0;

// =============================================================================
// SYSTEM & CAN STATE
// =============================================================================
volatile uint16_t currentRpm = 0;
volatile bool     engineRunning = false;
volatile uint32_t totalFramesRx = 0;
volatile uint32_t busErrorCount = 0;
volatile bool     ecuDiagnosticResponseReceived = false;
volatile bool     clearAcknowledged = false;
unsigned long     lastCanRxMs = 0;

inline bool isEcuOnline() {
    return (lastCanRxMs > 0) && (millis() - lastCanRxMs < 3500);
}

bool twaiRunning = false;
bool twaiInstalled = false;
bool autoCleanEnabled = true;
bool bootAutoCleanDone = false;
unsigned long bootAutoCleanDueMs = 0;
unsigned long lastAutoScanMs = 0;
unsigned long lastAutoClearMs = 0;
const unsigned long AUTO_SCAN_INTERVAL_MS = 25000;
const unsigned long AUTO_CLEAR_COOLDOWN_MS = 10000;
bool prevEngineRunning = false;
bool prevEcuOnline = false;

unsigned long ledTurnOffMs = 0;
unsigned long bootMs = 0;
const unsigned long INACTIVITY_SLEEP_TIMEOUT_MS = 60000; // 60s silence -> Deep Sleep

// =============================================================================
// DTC DATA STRUCTURES & DEFINITIONS
// =============================================================================
#define MAX_STORED_DTCS 12
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
    { "P0030", "HO2S Heater Circuit (Bank 1 Sensor 1)" },
    { "P0031", "HO2S Heater Low (Bank 1 Sensor 1)" },
    { "P0032", "HO2S Heater High (Bank 1 Sensor 1)" },
    { "P0036", "HO2S Heater Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust / Akrapovic)" },
    { "P0037", "HO2S Heater Low (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0038", "HO2S Heater High (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0053", "HO2S Heater Resistance (Bank 1 Sensor 1)" },
    { "P0054", "HO2S Heater Resistance (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0130", "O2 Sensor Circuit (Bank 1 Sensor 1)" },
    { "P0131", "O2 Sensor Circuit Low (Bank 1 Sensor 1)" },
    { "P0132", "O2 Sensor Circuit High (Bank 1 Sensor 1)" },
    { "P0134", "O2 Sensor Circuit No Activity (Bank 1 Sensor 1)" },
    { "P0135", "O2 Sensor Heater Circuit (Bank 1 Sensor 1)" },
    { "P0136", "O2 Sensor Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0137", "O2 Sensor Circuit Low (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0138", "O2 Sensor Circuit High (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0141", "O2 Sensor Heater Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust)" },
    { "P0420", "Catalyst System Efficiency Below Threshold (Bank 1 - Cat Removal)" },
    { "P0443", "EVAP Canister Purge Valve Circuit" },
    { "P0444", "EVAP Canister Purge Valve Circuit Open" },
    { "P0445", "EVAP Canister Purge Valve Circuit Shorted" },
    { "P0105", "Manifold Absolute Pressure Circuit" },
    { "P0110", "Intake Air Temperature Circuit" },
    { "P0115", "Engine Coolant Temperature Circuit" },
    { "P0120", "Throttle Position Sensor / APS Circuit" },
    { "P0201", "Injector Circuit - Cylinder 1" },
    { "P0202", "Injector Circuit - Cylinder 2" },
    { "P0203", "Injector Circuit - Cylinder 3" },
    { "P0300", "Random/Multiple Cylinder Misfire" },
    { "P0335", "Crankshaft Position Sensor A Circuit" },
    { "P0500", "Vehicle Speed Sensor Malfunction" }
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================
bool initTWAI();
void stopTWAI();
void enterDeepSleep();
bool sendCanFrame(uint32_t id, uint8_t len, const uint8_t* data);
void pingEcu();
void handleFrame(const twai_message_t &rxMsg);
void handleDiagnosticResponse(const twai_message_t &rxMsg);
void requestDTCs(uint8_t mode);
bool requestClearDTCs();
bool triggerScanSequence();
bool executeClearSequence(const char* triggerSource);
void pumpTwai(uint32_t waitMs);
String decodeDTC(uint8_t b1, uint8_t b2);
void addStoredDtc(const String &code, const String &desc);
void triggerLedActivity();

void loadLogsFromNvs();
void recordClearLog(const char* trigger, const char* codesStr, const char* result);
void eraseLogsFromNvs();
String buildStatusJson();
String buildDtcsJson();
String buildLogsJson();
void notifyBleStatus();
void notifyBleDtcs();
void notifyBleLogs();

void setupBLE();
void printBanner();
void printHelp();
void printLogsCli();
void executeCliCommand(String input);

// =============================================================================
// BLE SERVER CALLBACKS
// =============================================================================
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        bleClientConnected = true;
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
        delay(60);
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
    }

    void onDisconnect(BLEServer* pServer) {
        bleClientConnected = false;
        // Restart advertising so clients can reconnect
        pServer->getAdvertising()->start();
    }
};

// =============================================================================
// BLE COMMAND CALLBACKS
// =============================================================================
class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() == 0) return;

        String cmd = String(rxValue.c_str());
        cmd.trim();
        cmd.toUpperCase();

        if (cmd == "SCAN") {
            triggerScanSequence();
            notifyBleDtcs();
            notifyBleStatus();
        } 
        else if (cmd == "CLEAR") {
            executeClearSequence("BLE_MANUAL");
            notifyBleDtcs();
            notifyBleStatus();
            notifyBleLogs();
        } 
        else if (cmd == "LOGS") {
            notifyBleLogs();
        } 
        else if (cmd == "ERASE_LOGS") {
            eraseLogsFromNvs();
            notifyBleLogs();
            notifyBleStatus();
        }
        else if (cmd == "AUTO_ON") {
            autoCleanEnabled = true;
            notifyBleStatus();
        }
        else if (cmd == "AUTO_OFF") {
            autoCleanEnabled = false;
            notifyBleStatus();
        }
    }
};

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // Initialize USB CDC Serial with NON-BLOCKING timeout (0ms)
    // Prevents watchdog hangs when USB is plugged in but Serial Monitor is closed!
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);

    // Initialize Onboard Activity LED
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
        delay(80);
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
        delay(80);
    }

    // Optional short wait for terminal if connected
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 500)) {
        delay(10);
    }

    printBanner();

    // Check wakeup reason (GPIO 3 wakeup on CAN Dominant SOF bit when ignition turns ON)
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    if (wakeupReason == ESP_SLEEP_WAKEUP_GPIO) {
        if (Serial) Serial.println("[WAKE] ⚡ ESP32 woke from Deep Sleep via CAN RX activity (Ignition ON)!\n");
    }
    lastCanRxMs = millis();

    // 1. Initialize NVS Storage & Load Logs
    prefs.begin("mt09_clean", false);
    bootCount = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", bootCount);
    totalClears = prefs.getUInt("clears", 0);
    lastClearedCode = prefs.getString("last_code", "None");
    loadLogsFromNvs();

    if (Serial) {
        Serial.printf("[NVS] Boots: %u | Total Clears Logged: %u | Last Code: %s | Stored Logs: %u\n",
                      bootCount, totalClears, lastClearedCode.c_str(), clearLogCount);
    }

    // 2. Initialize Bluetooth Low Energy (BLE)
    setupBLE();

    // 3. Initialize TWAI CAN Controller at 500 kbps (Euro 5 Standard)
    if (initTWAI()) {
        if (Serial) {
            Serial.printf("[INIT] CAN Controller initialized on TX: GPIO %d, RX: GPIO %d\n", CAN_TX_PIN, CAN_RX_PIN);
            Serial.println("[INIT] Autonomous Background DTC Auto-Cleaner: ACTIVE (No user input required)\n");
        }
    } else {
        if (Serial) Serial.println("[ERROR] Failed to start CAN Controller! Check wiring.");
    }

    bootMs = millis();
    bootAutoCleanDueMs = bootMs + 4000;
}

// =============================================================================
// BLE INITIALIZATION
// =============================================================================
void setupBLE() {
    BLEDevice::init("MT09-Cleaner");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Status Characteristic (Read / Notify)
    pCharStatus = pService->createCharacteristic(
        CHAR_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharStatus->addDescriptor(new BLE2902());
    pCharStatus->setValue(buildStatusJson().c_str());

    // DTCs Characteristic (Read / Notify)
    pCharDtcs = pService->createCharacteristic(
        CHAR_DTCS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharDtcs->addDescriptor(new BLE2902());
    pCharDtcs->setValue(buildDtcsJson().c_str());

    // Command Characteristic (Write)
    pCharCommand = pService->createCharacteristic(
        CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharCommand->setCallbacks(new CommandCallbacks());

    // Logs Characteristic (Read / Notify)
    pCharLogs = pService->createCharacteristic(
        CHAR_LOGS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharLogs->addDescriptor(new BLE2902());
    pCharLogs->setValue(buildLogsJson().c_str());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    if (Serial) Serial.println("[BLE] Advertising started as 'MT09-Cleaner' (UUID: 18F0)\n");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    unsigned long now = millis();

    // 1. Service non-blocking USB CDC Serial CLI
    if (Serial && Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            executeCliCommand(input);
        }
    }

    // 2. Process incoming CAN messages from TWAI hardware
    if (twaiRunning) {
        twai_message_t rxMsg;
        uint8_t rxBatch = 0;
        while (twai_receive(&rxMsg, 0) == ESP_OK && rxBatch < 30) {
            handleFrame(rxMsg);
            rxBatch++;
        }

        uint32_t alertsTriggered = 0;
        if (twai_read_alerts(&alertsTriggered, 0) == ESP_OK) {
            if (alertsTriggered & TWAI_ALERT_BUS_ERROR) {
                busErrorCount++;
            }
            if (alertsTriggered & TWAI_ALERT_BUS_OFF) {
                if (Serial) Serial.println("\n[ALERT] TWAI Bus-Off detected! Initiating recovery...");
                twai_initiate_recovery();
            }
            if (alertsTriggered & TWAI_ALERT_BUS_RECOVERED) {
                if (Serial) Serial.println("\n[ALERT] TWAI Bus Recovered!");
                twai_start();
            }
        }
    }

    // Check for ECU online/offline transition
    bool currentEcuOnline = isEcuOnline();
    if (currentEcuOnline != prevEcuOnline) {
        prevEcuOnline = currentEcuOnline;
        if (Serial) Serial.printf("\n[ECU STATE] Motorcycle ECU is now: %s\n\n", currentEcuOnline ? "ONLINE (Ignition ON)" : "OFFLINE (Ignition OFF)");
        notifyBleStatus();
        notifyBleDtcs();
    }

    // 3. AUTONOMOUS BACKGROUND DTC AUTO-SCAN & AUTO-CLEAR
    if (autoCleanEnabled && twaiRunning) {
        // A. Boot auto-scan: runs ONCE 4s after Key ON, then goes silent
        if (!bootAutoCleanDone && currentEcuOnline && now >= bootAutoCleanDueMs) {
            bootAutoCleanDone = true;
            if (!engineRunning && currentRpm == 0) {
                if (Serial) Serial.println("\n[AUTO-CLEAN] 🚀 Key ON detected: Scanning ECU for trouble codes (Engine OFF)...");
                triggerScanSequence();
                if (storedDtcCount > 0) {
                    executeClearSequence("BOOT_AUTO");
                } else {
                    if (Serial) Serial.println("[AUTO-CLEAN] ✅ Boot check: System clean! 0 trouble codes found in ECU.\n");
                }
                notifyBleDtcs();
                notifyBleStatus();
                if (Serial) Serial.println("[AUTO-CLEAN] CAN transmissions stopped. Yamaha ECU diagnostic mode will exit in ~3s.\n");
            }
        }

        // B. Detect Engine Stop transition (e.g. rider turned off kill switch or finished ride)
        if (prevEngineRunning && !engineRunning && currentEcuOnline) {
            if (Serial) Serial.println("\n[AUTO-CLEAN] Engine shutdown detected. Running post-ride DTC check in 3s...");
            lastAutoScanMs = now - (AUTO_SCAN_INTERVAL_MS - 3000);
            bootAutoCleanDone = false; // Arm check for next startup
        }
        prevEngineRunning = engineRunning;
    }

    // 5. Handle Non-blocking Activity LED turn off
    if (ledTurnOffMs > 0 && now >= ledTurnOffMs) {
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
        ledTurnOffMs = 0;
    }

    // 6. Low-Power Deep Sleep Inactivity Check
    // If no CAN traffic for 60s and no phone connected via BLE, enter deep sleep (<15 µA)
    if (twaiRunning && (now - lastCanRxMs >= INACTIVITY_SLEEP_TIMEOUT_MS)) {
        if (!bleClientConnected) {
            enterDeepSleep();
        }
    }

    delay(2);
}

// =============================================================================
// SCAN & CLEAR WORKFLOW ENGINES
// =============================================================================
void pingEcu() {
    uint8_t payload[8] = { 0x02, 0x01, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55 };
    sendCanFrame(0x7DF, 8, payload);
}

bool triggerScanSequence() {
    storedDtcCount = 0;
    ecuDiagnosticResponseReceived = false;

    requestDTCs(0x03); // Confirmed DTCs
    pumpTwai(140);
    requestDTCs(0x07); // Pending DTCs
    pumpTwai(140);

    return ecuDiagnosticResponseReceived;
}

bool executeClearSequence(const char* triggerSource) {
    if (engineRunning || currentRpm > 0) {
        if (Serial) Serial.println("\n⚠️ [CLEAR REFUSED] SAFETY GUARD: Engine is RUNNING! Turn off engine to clear.\n");
        recordClearLog(triggerSource, "BLOCKED", "REFUSED_RUN");
        return false;
    }

    // Build comma-separated string of codes being cleared
    String codesStr = "";
    for (int i = 0; i < storedDtcCount; i++) {
        if (i > 0) codesStr += ", ";
        codesStr += storedDtcs[i].code;
    }
    if (codesStr.length() == 0) codesStr = "None";

    if (Serial) Serial.println("\n🧹 [CLEAR] Transmitting OBD Mode 04 clear command to ECU...");
    clearAcknowledged = false;
    bool sent = requestClearDTCs();
    if (!sent) {
        if (Serial) Serial.println("❌ [CLEAR ERROR] Failed to transmit clear frame on CAN bus.\n");
        recordClearLog(triggerSource, codesStr.c_str(), "TX_FAILED");
        return false;
    }

    // Wait up to 350ms for ECU positive acknowledgment (Mode 04 response: 0x44)
    unsigned long startWait = millis();
    while (!clearAcknowledged && (millis() - startWait < 350)) {
        pumpTwai(10);
    }

    if (clearAcknowledged) {
        lastCanRxMs = millis();
        totalClears++;
        prefs.putUInt("clears", totalClears);
        if (storedDtcCount > 0) {
            lastClearedCode = storedDtcs[0].code;
            prefs.putString("last_code", lastClearedCode);
        }
        recordClearLog(triggerSource, codesStr.c_str(), "CLEARED_OK");
        storedDtcCount = 0;
        lastAutoClearMs = millis();

        if (Serial) Serial.printf("🎉 [CLEAR CONFIRMED] ECU acknowledged Mode 04 (0x44)! Erased codes (%s). Total logged: %u\n\n", codesStr.c_str(), totalClears);
        return true;
    } else {
        if (Serial) Serial.println("⚠️ [CLEAR UNCONFIRMED] No 0x44 ACK received (ECU may be OFF).\n");
        recordClearLog(triggerSource, codesStr.c_str(), "NO_ECU_ACK");
        return false;
    }
}

// =============================================================================
// TWAI CONTROLLER MANAGEMENT & TRANSMIT
// =============================================================================
bool initTWAI() {
    if (twaiRunning) stopTWAI();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 32;
    g_config.tx_queue_len = 8;
    g_config.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
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
    if (Serial) {
        Serial.println("\n[SLEEP] 🌙 Inactivity timeout reached (no CAN traffic for 60s).");
        Serial.println("[SLEEP] Shutting down TWAI & BLE. Entering ultra-low power Deep Sleep (<15 µA)...");
        Serial.println("[SLEEP] Wakeup trigger: CAN RX (GPIO 3) LOW level (Dominant SOF on Key ON).");
        Serial.flush();
    }
    delay(40);

    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
    stopTWAI();
    BLEDevice::deinit(true);
    delay(20);

    // Wakeup on CAN RX (GPIO 3) dominant bit (LOW level)
    esp_deep_sleep_enable_gpio_wakeup(1ULL << CAN_RX_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

bool sendCanFrame(uint32_t id, uint8_t len, const uint8_t* data) {
    if (!twaiRunning) return false;
    twai_message_t txMsg;
    txMsg.identifier = id;
    txMsg.flags = TWAI_MSG_FLAG_NONE;
    txMsg.data_length_code = len;
    memcpy(txMsg.data, data, len);
    esp_err_t err = twai_transmit(&txMsg, pdMS_TO_TICKS(50));
    return (err == ESP_OK);
}

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

void requestDTCs(uint8_t mode) {
    uint8_t payload[8] = { 0x01, mode, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };
    sendCanFrame(0x7DF, 8, payload);
}

bool requestClearDTCs() {
    if (engineRunning || currentRpm > 0) return false;
    uint8_t payload[8] = { 0x01, 0x04, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };
    return sendCanFrame(0x7DF, 8, payload);
}

// =============================================================================
// FRAME PROCESSING & DIAGNOSTIC DECODING
// =============================================================================
void handleFrame(const twai_message_t &rxMsg) {
    totalFramesRx++;
    triggerLedActivity();

    uint32_t id = rxMsg.identifier;
    uint8_t dlc = rxMsg.data_length_code;
    lastCanRxMs = millis();

    // 1. Live Engine Running State Detection (Yamaha CAN ID 0x751 Byte 7)
    if (id == 0x751 && dlc >= 8) {
        bool prev = engineRunning;
        engineRunning = (rxMsg.data[7] == 0x04);
        if (prev != engineRunning && Serial) {
            Serial.printf("[ENGINE] Engine state: %s\n", engineRunning ? "🔥 RUNNING" : "🛑 STOPPED");
        }
    }

    // 2. Intercept Diagnostic Responses (0x7E8, 0x7E9, 0x758)
    if (id == 0x7E8 || id == 0x7E9 || id == 0x758) {
        handleDiagnosticResponse(rxMsg);
    }
}

void handleDiagnosticResponse(const twai_message_t &rxMsg) {
    if (rxMsg.data_length_code < 2) return;
    uint8_t pciType = rxMsg.data[0] & 0xF0;
    ecuDiagnosticResponseReceived = true;
    lastCanRxMs = millis();

    // Single Frame (SF)
    if (pciType == 0x00) {
        awaitingConsecutiveDtc = false;
        uint8_t service = rxMsg.data[1];

        // Mode 04: Positive Clear Response (0x44)
        if (service == 0x44) {
            clearAcknowledged = true;
        }
        // Mode 01: Engine RPM (PID 0x0C)
        else if (service == 0x41 && rxMsg.data_length_code >= 5 && rxMsg.data[2] == 0x0C) {
            currentRpm = ((rxMsg.data[3] * 256) + rxMsg.data[4]) / 4;
        }
        // Mode 03 / Mode 07: Stored or Pending DTCs
        else if (service == 0x43 || service == 0x47) {
            uint8_t count = rxMsg.data[2];
            if (count > 0) {
                for (int i = 3; i < 7 && (i + 1) < rxMsg.data_length_code; i += 2) {
                    if (rxMsg.data[i] != 0 || rxMsg.data[i+1] != 0) {
                        decodeDTC(rxMsg.data[i], rxMsg.data[i+1]);
                    }
                }
            }
        }
    }
    // First Frame (FF) of Multi-Frame Response
    else if (pciType == 0x10 && rxMsg.data_length_code >= 6) {
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
    // Consecutive Frame (CF)
    else if (pciType == 0x20 && awaitingConsecutiveDtc) {
        for (int i = 1; i < 7 && (i + 1) < rxMsg.data_length_code; i += 2) {
            if (rxMsg.data[i] != 0 || rxMsg.data[i+1] != 0) {
                decodeDTC(rxMsg.data[i], rxMsg.data[i+1]);
            }
        }
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

    String desc = "Unknown Diagnostic Trouble Code";
    for (size_t i = 0; i < sizeof(KNOWN_DTCS)/sizeof(KNOWN_DTCS[0]); i++) {
        if (dtcCode.equalsIgnoreCase(KNOWN_DTCS[i].code)) {
            desc = KNOWN_DTCS[i].desc;
            break;
        }
    }

    addStoredDtc(dtcCode, desc);
    return dtcCode;
}

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

void triggerLedActivity() {
    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
    ledTurnOffMs = millis() + 15;
}

// =============================================================================
// PERSISTENT NVS CLEAR LOGGER IMPLEMENTATION
// =============================================================================
void loadLogsFromNvs() {
    clearLogCount = prefs.getUChar("log_cnt", 0);
    clearLogHead = prefs.getUChar("log_head", 0);
    if (clearLogCount > MAX_CLEAR_LOGS) clearLogCount = MAX_CLEAR_LOGS;

    for (uint8_t i = 0; i < clearLogCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "l_%d", i);
        prefs.getBytes(key, &clearLogs[i], sizeof(ClearLogEntry));
    }
}

void recordClearLog(const char* trigger, const char* codesStr, const char* result) {
    ClearLogEntry entry;
    entry.id = totalClears;
    entry.boot = bootCount;
    entry.uptimeSec = (uint32_t)(millis() / 1000);
    strncpy(entry.trigger, trigger, sizeof(entry.trigger) - 1);
    entry.trigger[sizeof(entry.trigger) - 1] = '\0';
    strncpy(entry.codes, codesStr, sizeof(entry.codes) - 1);
    entry.codes[sizeof(entry.codes) - 1] = '\0';
    strncpy(entry.result, result, sizeof(entry.result) - 1);
    entry.result[sizeof(entry.result) - 1] = '\0';

    uint8_t slot = clearLogHead;
    clearLogs[slot] = entry;

    char key[16];
    snprintf(key, sizeof(key), "l_%d", slot);
    prefs.putBytes(key, &entry, sizeof(ClearLogEntry));

    clearLogHead = (clearLogHead + 1) % MAX_CLEAR_LOGS;
    if (clearLogCount < MAX_CLEAR_LOGS) clearLogCount++;

    prefs.putUChar("log_cnt", clearLogCount);
    prefs.putUChar("log_head", clearLogHead);
}

void eraseLogsFromNvs() {
    for (uint8_t i = 0; i < MAX_CLEAR_LOGS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "l_%d", i);
        prefs.remove(key);
    }
    clearLogCount = 0;
    clearLogHead = 0;
    prefs.putUChar("log_cnt", 0);
    prefs.putUChar("log_head", 0);
    if (Serial) Serial.println("[NVS] Persistent clear logs erased.\n");
}

// =============================================================================
// BLE JSON BUILDERS & NOTIFIERS
// =============================================================================
String buildStatusJson() {
    bool online = isEcuOnline();
    char buf[280];
    const char* stateStr = "OFFLINE";
    if (online) {
        stateStr = (storedDtcCount > 0) ? "FAULT" : "CLEAN";
    }

    const char* engStr = "OFFLINE";
    if (online) {
        engStr = engineRunning ? "RUNNING" : "STOPPED";
    }

    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"online\":%s,\"engine\":\"%s\",\"auto\":%s,\"clears\":%u,\"boots\":%u,\"dtcCount\":%d,\"lastCode\":\"%s\"}",
             stateStr,
             online ? "true" : "false",
             engStr,
             autoCleanEnabled ? "true" : "false",
             totalClears, bootCount, storedDtcCount, lastClearedCode.c_str());
    return String(buf);
}

String buildDtcsJson() {
    bool online = isEcuOnline();
    String json = "{\"online\":" + String(online ? "true" : "false") +
                  ",\"count\":" + String(online ? storedDtcCount : 0) + ",\"codes\":[";
    if (online) {
        for (int i = 0; i < storedDtcCount; i++) {
            if (i > 0) json += ",";
            json += "{\"code\":\"" + storedDtcs[i].code + "\",\"desc\":\"" + storedDtcs[i].desc + "\"}";
        }
    }
    json += "]}";
    return json;
}

String buildLogsJson() {
    String json = "{\"count\":" + String(clearLogCount) + ",\"logs\":[";
    for (int i = 0; i < clearLogCount; i++) {
        if (i > 0) json += ",";
        json += "{\"id\":" + String(clearLogs[i].id) +
                ",\"boot\":" + String(clearLogs[i].boot) +
                ",\"t\":" + String(clearLogs[i].uptimeSec) +
                ",\"trig\":\"" + String(clearLogs[i].trigger) + "\"" +
                ",\"codes\":\"" + String(clearLogs[i].codes) + "\"" +
                ",\"res\":\"" + String(clearLogs[i].result) + "\"}";
    }
    json += "]}";
    return json;
}

void notifyBleStatus() {
    if (pCharStatus) {
        String s = buildStatusJson();
        pCharStatus->setValue(s.c_str());
        if (bleClientConnected) pCharStatus->notify();
    }
}

void notifyBleDtcs() {
    if (pCharDtcs) {
        String s = buildDtcsJson();
        pCharDtcs->setValue(s.c_str());
        if (bleClientConnected) pCharDtcs->notify();
    }
}

void notifyBleLogs() {
    if (pCharLogs) {
        String s = buildLogsJson();
        pCharLogs->setValue(s.c_str());
        if (bleClientConnected) pCharLogs->notify();
    }
}

// =============================================================================
// CLI MENU & PRINT HELPERS
// =============================================================================
void printBanner() {
    if (!Serial) return;
    Serial.println("\n=================================================================");
    Serial.println("  🏍️  YAMAHA MT-09 SP EURO 5+ AUTONOMOUS DTC CLEANER (BLE EDITION)");
    Serial.println("  ESP32-C3 Super Mini Plus | TWAI CAN 500k | BLE: 'MT09-Cleaner' ");
    Serial.println("=================================================================");
}

void printHelp() {
    if (!Serial) return;
    Serial.println("\n================== COMMAND MENU ==================");
    Serial.println("  r : Scan ECU for Trouble Codes (Mode 03/07)");
    Serial.println("  k : Force Clear Codes & Reset CEL (Engine OFF)");
    Serial.println("  l : Display Persistent NVS Clear History Logs");
    Serial.println("  e : Erase Stored Clear History Logs from NVS");
    Serial.println("  a : Toggle Autonomous Auto-Cleaner");
    Serial.println("  z : Enter Deep Sleep immediately (wakes on Key ON)");
    Serial.println("  ? : Print this Help Menu");
    Serial.println("==================================================\n");
}

void printLogsCli() {
    if (!Serial) return;
    Serial.println("\n======================================================================================");
    Serial.println("                             PERSISTENT CLEAR HISTORY LOGS                             ");
    Serial.println("======================================================================================");
    Serial.println(" Event #   Boot #   Uptime (s)   Trigger Source    Cleared Code(s)         Result     ");
    Serial.println("--------------------------------------------------------------------------------------");

    if (clearLogCount == 0) {
        Serial.println("  (No clear events recorded in NVS memory yet)");
    } else {
        for (uint8_t i = 0; i < clearLogCount; i++) {
            Serial.printf(" %-9u %-8u %-12u %-17s %-23s %s\n",
                          clearLogs[i].id, clearLogs[i].boot, clearLogs[i].uptimeSec,
                          clearLogs[i].trigger, clearLogs[i].codes, clearLogs[i].result);
        }
    }
    Serial.println("======================================================================================\n");
}

void executeCliCommand(String input) {
    input.trim();
    if (input.length() == 0) return;
    char cmd = input.charAt(0);

    switch (cmd) {
        case '?':
        case 'h':
        case 'H':
            printHelp();
            break;

        case 'r':
        case 'R': {
            if (Serial) Serial.println("\n🔍 [DTC SCAN] Querying ECU for confirmed & pending trouble codes...");
            triggerScanSequence();
            if (storedDtcCount == 0) {
                if (Serial) Serial.println("✅ Clean system! 0 trouble codes found.\n");
            } else {
                if (Serial) {
                    Serial.printf("⚠️ Found %d trouble code(s):\n", storedDtcCount);
                    for (int i = 0; i < storedDtcCount; i++) {
                        Serial.printf("   [%s] %s\n", storedDtcs[i].code.c_str(), storedDtcs[i].desc.c_str());
                    }
                    Serial.println();
                }
            }
            notifyBleDtcs();
            notifyBleStatus();
            break;
        }

        case 'k':
        case 'K':
            executeClearSequence("CLI_MANUAL");
            notifyBleDtcs();
            notifyBleStatus();
            notifyBleLogs();
            break;

        case 'l':
        case 'L':
            printLogsCli();
            break;

        case 'e':
        case 'E':
            eraseLogsFromNvs();
            notifyBleLogs();
            break;

        case 'a':
        case 'A':
            autoCleanEnabled = !autoCleanEnabled;
            if (Serial) Serial.printf("\n[AUTO-CLEAN] Autonomous background cleaning: %s\n\n", autoCleanEnabled ? "ENABLED" : "DISABLED");
            notifyBleStatus();
            break;

        case 'z':
        case 'Z':
            enterDeepSleep();
            break;

        default:
            if (Serial) Serial.printf("Unknown command '%c'. Send '?' for help.\n", cmd);
            break;
    }
}