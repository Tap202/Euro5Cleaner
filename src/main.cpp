#include <Arduino.h>
#include "driver/twai.h"
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// ==========================================
// PIN DEFINITIONS & CAN CONFIGURATION
// ==========================================
#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

#define OBD_BROADCAST_ID 0x7DF
#define OBD_PHYSICAL_ID  0x7E0
#define OBD_RESPONSE_MIN 0x7E8
#define OBD_RESPONSE_MAX 0x7EF

const unsigned long POLL_INTERVAL      = 5000;  // Poll DTCs every 5s
const unsigned long CLEAR_COOLDOWN     = 30000; // 30s cooldown between clears
const unsigned long RPM_POLL_INTERVAL  = 2000;  // Poll RPM every 2s when waiting to clear

unsigned long lastPollTime        = 0;
unsigned long lastClearTime       = (unsigned long)-CLEAR_COOLDOWN; 
unsigned long lastRpmRequestTime  = 0;
unsigned long lastCanRxTime       = 0;
bool clearRequested               = false;

// ==========================================
// ISO-TP MULTI-FRAME BUFFER
// ==========================================
#define RX_BUFFER_SIZE 128
uint8_t rxBuffer[RX_BUFFER_SIZE];
uint16_t rxTotalLength            = 0;
uint16_t rxIndex                  = 0;
uint8_t expectedSeqNum            = 1;
bool isReceivingMultiFrame        = false;
unsigned long multiFrameTimeout   = 0;
const unsigned long MULTI_FRAME_TIMEOUT_MS = 250;

// ==========================================
// LIVE TELEMETRY VARIABLES
// ==========================================
bool liveTelemetryMode = false;
unsigned long lastTelemetryTime = 0;
uint8_t telemetryStep = 0;

int liveRPM = 0;
int liveTempC = 0;
int liveTPS = 0;
int liveIAT = 0;
float liveVoltage = 0.0;

unsigned long lastVoltageAlertTime = 0;
const unsigned long VOLTAGE_ALERT_COOLDOWN = 30000; // 30-second cooldown between alerts
const float VOLTAGE_MIN_THRESHOLD = 12.5;           // Alert if running voltage drops below this

// ==========================================
// WI-FI & OTA CONFIGURATION
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

bool otaMode = false;
volatile bool requestOTA = false;

// ==========================================
// NVS STORAGE (PREFERENCES)
// ==========================================
Preferences preferences;

struct DTCRecord {
    uint32_t bootCount;
    uint32_t uptimeSecs;
    char type;        // 'P', 'C', 'B', 'U'
    uint16_t code;    // Hex code
};

#define MAX_HISTORY 10
DTCRecord dtcHistory[MAX_HISTORY];
uint8_t historyIndex     = 0;
uint32_t currentBootCount = 0;

// ==========================================
// BLE UART CONFIGURATION (NORDIC UART)
// ==========================================
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected         = false;
bool oldDeviceConnected      = false;
volatile bool requestPrintHistory = false;
volatile bool requestCSVExport = false;

// Forward declarations
void bleSendLine(const char* text);
void printDTCHistory();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
    }
    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
    }
};

class MyRxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            char cmd = rxValue[0];

            if (cmd == 'h' || cmd == 'H' || cmd == '?') {
                requestPrintHistory = true;
            } 
            else if (cmd == 'u' || cmd == 'U') {
                requestOTA = true;
            } 
            else if (cmd == 'c' || cmd == 'C') {
                requestCSVExport = true;
            }
            else if (cmd == 'l' || cmd == 'L') {
                liveTelemetryMode = !liveTelemetryMode;
                if (liveTelemetryMode) {
                    Serial.println("[BLE] Live Telemetry Started");
                    bleSendLine("[SYSTEM] Live Telemetry Started");
                } else {
                    Serial.println("[BLE] Live Telemetry Stopped");
                    bleSendLine("[SYSTEM] Live Telemetry Stopped");
                }
            }
        }
    }
};

// ==========================================
// LOGGING & NVS HELPERS
// ==========================================
void logClearedDTC(char type, uint16_t code) {
    dtcHistory[historyIndex].bootCount  = currentBootCount;
    dtcHistory[historyIndex].uptimeSecs = millis() / 1000;
    dtcHistory[historyIndex].type       = type;
    dtcHistory[historyIndex].code       = code;

    historyIndex = (historyIndex + 1) % MAX_HISTORY;

    preferences.putBytes("history", &dtcHistory, sizeof(dtcHistory));
    preferences.putUChar("hist_idx", historyIndex);

    Serial.printf("[NVS] Logged code %c%04X to flash.\n", type, code);
}

void bleSendLine(const char* text) {
    if (deviceConnected && pTxCharacteristic != NULL) {
        pTxCharacteristic->setValue((uint8_t*)text, strlen(text));
        pTxCharacteristic->notify();
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

void printDTCHistory() {
    Serial.println("\n--- Cleared DTC History (Last 10) ---");
    bleSendLine("\n--- Cleared DTC History (Last 10) ---");

    bool empty = true;
    for (int i = 0; i < MAX_HISTORY; i++) {
        int readIndex = (historyIndex + i) % MAX_HISTORY;
        if (dtcHistory[readIndex].bootCount == 0) continue;

        empty = false;
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Ride #%u | Engine Run Time: %us | Code Cleared: %c%04X",
                 dtcHistory[readIndex].bootCount,
                 dtcHistory[readIndex].uptimeSecs,
                 dtcHistory[readIndex].type,
                 dtcHistory[readIndex].code);

        Serial.println(buffer);
        bleSendLine(buffer);
    }

    if (empty) {
        Serial.println("No codes have been cleared yet.");
        bleSendLine("No codes have been cleared yet.");
    }
    Serial.println("-------------------------------------\n");
    bleSendLine("-------------------------------------\n");
}

void exportHistoryCSV() {
    Serial.println("\n[BLE] Exporting DTC History as CSV...");
    bleSendLine("BootCount,Uptime(s),DTC_Cleared");
    
    bool empty = true;
    for (int i = 0; i < MAX_HISTORY; i++) {
        int readIndex = (historyIndex + i) % MAX_HISTORY;
        if (dtcHistory[readIndex].bootCount == 0) continue;

        empty = false;
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%u,%u,%c%04X",
                 dtcHistory[readIndex].bootCount,
                 dtcHistory[readIndex].uptimeSecs,
                 dtcHistory[readIndex].type,
                 dtcHistory[readIndex].code);

        bleSendLine(buffer);
    }

    if (empty) {
        bleSendLine("No data,0,N/A");
    }
    bleSendLine("---END OF CSV---");
}

// ==========================================
// OTA (OVER-THE-AIR) UPDATE MODE
// ==========================================
void enterOTAMode() {
    Serial.println("\n[OTA] Update mode requested! Shutting down CAN and BLE...");
    bleSendLine("Rebooting into Wi-Fi OTA mode...");
    delay(500); 

    twai_stop();
    twai_driver_uninstall();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("[OTA] Connecting to Wi-Fi");
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[OTA] Wi-Fi failed! Rebooting back to normal mode...");
        delay(1000);
        ESP.restart(); 
    }

    Serial.println("\n[OTA] Wi-Fi Connected!");
    Serial.print("[OTA] IP Address: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.setHostname("MT09_OBD_Tool");
    ArduinoOTA.setPassword("Mt09Sp2026!"); 

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update Success! Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        ESP.restart(); 
    });

    ArduinoOTA.begin();
    otaMode = true;
    Serial.println("[OTA] Ready in Arduino IDE Network Ports!");
}

// ==========================================
// CAN / TWAI TRANSMISSION
// ==========================================
void sendOBDRequest(uint8_t mode, uint8_t pid = 0xFF, uint32_t target_id = OBD_BROADCAST_ID) {
    twai_message_t request;
    request.identifier = target_id;
    request.flags = TWAI_MSG_FLAG_NONE;
    request.data_length_code = 8;

    if (mode == 0x03 || mode == 0x04) {
        request.data[0] = 0x01;
        request.data[1] = mode;
        for (int i = 2; i < 8; i++) request.data[i] = 0xAA; 
    } else {
        request.data[0] = 0x02;
        request.data[1] = mode;
        request.data[2] = pid;
        for (int i = 3; i < 8; i++) request.data[i] = 0xAA;
    }

    twai_transmit(&request, pdMS_TO_TICKS(50));
}

void sendFlowControl() {
    twai_message_t fc;
    fc.identifier = OBD_PHYSICAL_ID;
    fc.flags = TWAI_MSG_FLAG_NONE;
    fc.data_length_code = 8;
    fc.data[0] = 0x30; 
    fc.data[1] = 0x00; 
    fc.data[2] = 0x14; 
    
    for (int i = 3; i < 8; i++) fc.data[i] = 0xAA;

    twai_transmit(&fc, pdMS_TO_TICKS(50));
}

void executeClear() {
    Serial.println("[OBD] Engine off (0 RPM) confirmed. Sending Mode 04...");
    bleSendLine("[OBD] Clearing DTCs now...");
    sendOBDRequest(0x04, 0xFF, OBD_PHYSICAL_ID);
    lastClearTime = millis();
    clearRequested = false;
}

void checkBusHealth() {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
            Serial.println("[CAN] Bus off detected! Initiating recovery...");
            twai_initiate_recovery();
        }
        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            Serial.println("[CAN] Bus recovered! Restarting driver...");
            twai_start();
        }
    }
}

// ==========================================
// DTC & OBD PROCESSING
// ==========================================
void processDTCs(const uint8_t* payload, int payloadLength) {
    int numBytes = payloadLength - 1; 
    int numCodes = numBytes / 2;
    bool faultMatched = false;

    for (int i = 0; i < numCodes; i++) {
        int offset = 1 + (i * 2);
        uint8_t b1 = payload[offset];
        uint8_t b2 = payload[offset + 1];

        if (b1 == 0 && b2 == 0) continue;

        char typeChar = "PCBU"[(b1 >> 6) & 0x03];
        uint16_t code = ((b1 & 0x3F) << 8) | b2;

        Serial.printf("[DTC] Found %c%04X\n", typeChar, code);

        if (typeChar == 'P') {
            if ((code >= 0x0130 && code <= 0x0167) || 
                (code >= 0x0420 && code <= 0x0439) ||
                (code >= 0x0470 && code <= 0x0480)) {
                faultMatched = true;
                logClearedDTC(typeChar, code);
            }
        }
    }

    if (faultMatched && !clearRequested && (millis() - lastClearTime >= CLEAR_COOLDOWN)) {
        Serial.println("[OBD] Target exhaust code identified. Queuing clear command.");
        clearRequested = true;
    }
}

void parseOBDResponse(const twai_message_t &msg) {
    uint8_t pciType = (msg.data[0] & 0xF0) >> 4;

    // --- 1. SINGLE FRAME ---
    if (pciType == 0x0) {
        uint8_t mode = msg.data[1];

        if (mode == 0x41) {
            uint8_t pid = msg.data[2];

            if (pid == 0x0C) { // RPM
                liveRPM = ((msg.data[3] << 8) | msg.data[4]) / 4;
                if (clearRequested) {
                    if (liveRPM == 0) executeClear();
                    else if (!liveTelemetryMode) Serial.println("[OBD] Engine running. Waiting for 0 RPM...");
                }
            }
            else if (pid == 0x05) { // Coolant Temp
                liveTempC = msg.data[3] - 40; 
            }
            else if (pid == 0x11) { // Throttle Position
                liveTPS = (msg.data[3] * 100) / 255;
            }
            else if (pid == 0x0F) { // Intake Air Temp
                liveIAT = msg.data[3] - 40;
            }
            else if (pid == 0x42) { // Control Module Voltage
                liveVoltage = ((msg.data[3] << 8) | msg.data[4]) / 1000.0;
                
                // Low Voltage Alert Logic
                if (liveRPM > 800 && liveVoltage < VOLTAGE_MIN_THRESHOLD) {
                    if (millis() - lastVoltageAlertTime >= VOLTAGE_ALERT_COOLDOWN) {
                        lastVoltageAlertTime = millis();
                        
                        char alertBuffer[64];
                        snprintf(alertBuffer, sizeof(alertBuffer), "⚠️ ALARM: Low Charging Voltage! %.1fV", liveVoltage);
                        
                        bleSendLine(alertBuffer);
                        Serial.println(alertBuffer);
                    }
                }
            }

            // Fire the telemetry string when the final PID (0x42) arrives
            if (liveTelemetryMode && pid == 0x42) {
                char tBuffer[128]; 
                snprintf(tBuffer, sizeof(tBuffer), "RPM:%04d | Coolant:%dC | IAT:%dC | TPS:%02d%% | Batt:%.1fV", 
                         liveRPM, liveTempC, liveIAT, liveTPS, liveVoltage);
                bleSendLine(tBuffer);
            }
            return;
        }

        if (mode == 0x43 || mode == 0x47) {
            uint8_t dataLen = msg.data[0] & 0x0F;
            if (dataLen < 3) return;
            processDTCs(&msg.data[1], dataLen);
        }
    }
    // --- 2. FIRST FRAME ---
    else if (pciType == 0x1) {
        uint8_t mode = msg.data[2];
        if (mode == 0x43 || mode == 0x47) {
            rxTotalLength = ((msg.data[0] & 0x0F) << 8) | msg.data[1];

            if (rxTotalLength > RX_BUFFER_SIZE) return;

            rxIndex = 0;
            for (int i = 2; i < 8; i++) {
                rxBuffer[rxIndex++] = msg.data[i];
            }

            isReceivingMultiFrame = true;
            expectedSeqNum = 1;
            multiFrameTimeout = millis();

            sendFlowControl();
        }
    }
    // --- 3. CONSECUTIVE FRAME ---
    else if (pciType == 0x2) {
        if (!isReceivingMultiFrame) return;

        uint8_t seqNum = msg.data[0] & 0x0F;
        if (seqNum != expectedSeqNum) {
            isReceivingMultiFrame = false;
            return;
        }

        for (int i = 1; i < 8; i++) {
            if (rxIndex < rxTotalLength) {
                rxBuffer[rxIndex++] = msg.data[i];
            }
        }

        expectedSeqNum = (expectedSeqNum + 1) & 0x0F;
        multiFrameTimeout = millis();

        if (rxIndex >= rxTotalLength) {
            isReceivingMultiFrame = false;
            processDTCs(rxBuffer, rxTotalLength);
        }
    }
}

// ==========================================
// SETUP & INITIALIZATION
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nMT-09 SP Exhaust DTC Cleaner Initializing...");

    // 1. Initialize NVS
    preferences.begin("obd_data", false);
    currentBootCount = preferences.getUInt("boot_cnt", 0) + 1;
    preferences.putUInt("boot_cnt", currentBootCount);
    preferences.getBytes("history", &dtcHistory, sizeof(dtcHistory));
    historyIndex = preferences.getUChar("hist_idx", 0);

    // 2. Initialize BLE UART
    BLEDevice::setMTU(517); 
    BLEDevice::init("MT09_OBD_BLE");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setCallbacks(new MyRxCallbacks());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("[BLE] Advertising started as 'MT09_OBD_BLE'");

    // 3. Initialize CAN/TWAI Driver
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    
    g_config.rx_queue_len = 50; 
    g_config.tx_queue_len = 10;
    
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    f_config.acceptance_code = (OBD_RESPONSE_MIN << 21);
    f_config.acceptance_mask = ~(0x7F8 << 21);
    f_config.single_filter   = true;

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK || twai_start() != ESP_OK) {
        Serial.println("[CAN] Failed to start TWAI driver!");
        return;
    }

    Serial.println("[CAN] Bus running at 500 kbps with hardware filtering.");
    printDTCHistory();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
    // --- 1. HANDLE OTA STATE ---
    if (requestOTA) {
        if (liveRPM > 0 && liveTelemetryMode) {
            bleSendLine("[ERROR] Cannot start OTA while engine is running!");
            Serial.println("[OTA] Rejected. Engine is running.");
        } else {
            enterOTAMode();
        }
        requestOTA = false;
    }

    if (otaMode) {
        ArduinoOTA.handle(); // Lock loop to Wi-Fi traffic only
        delay(10);
        return; 
    }

    // --- 2. NORMAL MOTORCYCLE STATE ---
    checkBusHealth();

    if (isReceivingMultiFrame && (millis() - multiFrameTimeout > MULTI_FRAME_TIMEOUT_MS)) {
        Serial.println("[CAN] ISO-TP Multi-frame timeout! Resetting buffer.");
        isReceivingMultiFrame = false;
        rxIndex = 0;
        rxTotalLength = 0;
    }

    twai_message_t rx_msg;
    while (twai_receive(&rx_msg, pdMS_TO_TICKS(1)) == ESP_OK) {
        lastCanRxTime = millis(); 
        if (rx_msg.identifier >= OBD_RESPONSE_MIN && rx_msg.identifier <= OBD_RESPONSE_MAX) {
            parseOBDResponse(rx_msg);
        }
    }

    if (millis() - lastCanRxTime > 3000) {
        if (liveRPM > 0) {
            liveRPM = 0; 
        }
        if (clearRequested) {
            Serial.println("[OBD] ECU asleep before clear executed. Waiting for next ignition.");
            clearRequested = false; 
        }
    }

    if (requestPrintHistory) {
        printDTCHistory();
        requestPrintHistory = false;
    }

    if (requestCSVExport) {
        exportHistoryCSV();
        requestCSVExport = false;
    }

    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("[BLE] Disconnected. Re-advertising...");
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
        Serial.println("[BLE] Device connected.");
    }

    // ==========================================
    // STATE MACHINE: TELEMETRY vs DTC POLLING
    // ==========================================
    if (liveTelemetryMode) {
        if (millis() - lastTelemetryTime >= 100) {
            lastTelemetryTime = millis();

            if (telemetryStep == 0) {
                sendOBDRequest(0x01, 0x0C); // RPM
                telemetryStep = 1;
            } else if (telemetryStep == 1) {
                sendOBDRequest(0x01, 0x05); // Coolant Temp
                telemetryStep = 2;
            } else if (telemetryStep == 2) {
                sendOBDRequest(0x01, 0x11); // TPS
                telemetryStep = 3;
            } else if (telemetryStep == 3) {
                sendOBDRequest(0x01, 0x0F); // Intake Air Temp
                telemetryStep = 4;
            } else if (telemetryStep == 4) {
                sendOBDRequest(0x01, 0x42); // Control Module Voltage
                telemetryStep = 0; 
            }
        }
    } 
    else if (clearRequested) {
        if (millis() - lastRpmRequestTime >= RPM_POLL_INTERVAL) {
            lastRpmRequestTime = millis();
            sendOBDRequest(0x01, 0x0C); 
        }
    } 
    else {
        if (millis() - lastPollTime >= POLL_INTERVAL) {
            lastPollTime = millis();
            sendOBDRequest(0x03); 
        }
    }

    delay(1); 
}