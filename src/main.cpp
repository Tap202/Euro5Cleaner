#include <Arduino.h>
#include "driver/twai.h"
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ==========================================
// PIN DEFINITIONS & CAN CONFIGURATION
// ==========================================
#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

#define OBD_BROADCAST_ID 0x7DF
#define OBD_PHYSICAL_ID  0x7E0
#define OBD_RESPONSE_MIN 0x7E8
#define OBD_RESPONSE_MAX 0x7EF

const unsigned long POLL_INTERVAL      = 5000;
const unsigned long CLEAR_COOLDOWN     = 30000;
const unsigned long RPM_POLL_INTERVAL  = 2000;

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
bool telemetryPending = false;                     
unsigned long telemetrySentTime = 0;               
uint8_t telemetryStep = 0;

int liveRPM = 0;
int liveTempC = 0;
int liveTPS = 0;
int liveIAT = 0;
float liveVoltage = 0.0;

unsigned long lastVoltageAlertTime = 0;
const unsigned long VOLTAGE_ALERT_COOLDOWN = 30000; 
const float VOLTAGE_MIN_THRESHOLD = 12.5;           

// ==========================================
// THREAD-SAFE FLAGS (RTOS & BLE CALLBACKS)
// ==========================================
std::atomic<bool> requestTelemetryToggleAlert(false);
std::atomic<bool> requestOTA(false);
std::atomic<bool> requestPrintHistory(false);
std::atomic<bool> requestCSVExport(false);
std::atomic<bool> telemetryUpdated(false);

bool otaMode = false;

// ==========================================
// NVS STORAGE (PREFERENCES)
// ==========================================
Preferences preferences;

struct DTCRecord {
    uint32_t bootCount;
    uint32_t uptimeSecs;
    char type;        
    uint16_t code;    
};

#define MAX_HISTORY 10
DTCRecord dtcHistory[MAX_HISTORY];
uint8_t historyIndex     = 0;
uint32_t currentBootCount = 0;

// ==========================================
// BLE UART CONFIGURATION & RTOS QUEUE
// ==========================================
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected         = false;
bool oldDeviceConnected      = false;

// FreeRTOS Queue for decoupled BLE sending
#define BLE_MSG_MAX_LEN 128
#define BLE_MSG_QUEUE_LEN 50 // [FIX] Increased buffer size to support full CSV dumps
QueueHandle_t bleMsgQueue;

void bleSendLine(const char* text);
void printDTCHistory();

// Dedicated task for transmitting BLE messages without blocking CAN
void bleTxTask(void *pvParameters) {
    char msg[BLE_MSG_MAX_LEN];
    for (;;) {
        if (xQueueReceive(bleMsgQueue, &msg, portMAX_DELAY) == pdPASS) {
            if (deviceConnected && pTxCharacteristic != NULL) {
                pTxCharacteristic->setValue((uint8_t*)msg, strlen(msg));
                pTxCharacteristic->notify();
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) override { deviceConnected = false; }
};

class MyRxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            char cmd = rxValue[0];
            if (cmd == 'h' || cmd == 'H' || cmd == '?') requestPrintHistory = true;
            else if (cmd == 'u' || cmd == 'U') requestOTA = true;
            else if (cmd == 'c' || cmd == 'C') requestCSVExport = true;
            else if (cmd == 'l' || cmd == 'L') {
                liveTelemetryMode = !liveTelemetryMode;
                requestTelemetryToggleAlert = true; 
            }
        }
    }
};

// ==========================================
// LOGGING & NVS HELPERS
// ==========================================
void logClearedDTC(char type, uint16_t code) {
    for (int i = 0; i < MAX_HISTORY; i++) {
        if (dtcHistory[i].bootCount == currentBootCount &&
            dtcHistory[i].type == type && dtcHistory[i].code == code) {
            Serial.printf("[NVS] Code %c%04X already logged this boot cycle.\n", type, code);
            return;
        }
    }

    dtcHistory[historyIndex].bootCount  = currentBootCount;
    dtcHistory[historyIndex].uptimeSecs = millis() / 1000;
    dtcHistory[historyIndex].type       = type;
    dtcHistory[historyIndex].code       = code;

    char key[10];
    snprintf(key, sizeof(key), "dtc_%d", historyIndex);
    preferences.putBytes(key, &dtcHistory[historyIndex], sizeof(DTCRecord));
    
    historyIndex = (historyIndex + 1) % MAX_HISTORY;
    preferences.putUChar("hist_idx", historyIndex);

    Serial.printf("[NVS] Logged code %c%04X to flash.\n", type, code);
}

// Thread-safe, non-blocking queue push for BLE messages
void bleSendLine(const char* text) {
    if (bleMsgQueue != NULL) {
        char buffer[BLE_MSG_MAX_LEN] = {0};
        strncpy(buffer, text, BLE_MSG_MAX_LEN - 1);
        // [FIX] Wait up to 10 ticks for space, applying backpressure to avoid dropped messages
        xQueueSend(bleMsgQueue, buffer, pdMS_TO_TICKS(10)); 
    }
}

void printDTCHistory() {
    Serial.println("\n--- Cleared DTC History ---");
    bleSendLine("\n--- Cleared DTC History ---");

    bool empty = true;
    for (int i = 0; i < MAX_HISTORY; i++) {
        int readIndex = (historyIndex + i) % MAX_HISTORY;
        if (dtcHistory[readIndex].bootCount == 0) continue;

        empty = false;
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Ride #%u | %us | Code: %c%04X",
                 dtcHistory[readIndex].bootCount, dtcHistory[readIndex].uptimeSecs,
                 dtcHistory[readIndex].type, dtcHistory[readIndex].code);

        Serial.println(buffer);
        bleSendLine(buffer);
    }
    if (empty) bleSendLine("No codes cleared yet.");
}

void exportHistoryCSV() {
    bleSendLine("BootCount,Uptime(s),DTC_Cleared");
    bool empty = true;
    for (int i = 0; i < MAX_HISTORY; i++) {
        int readIndex = (historyIndex + i) % MAX_HISTORY;
        if (dtcHistory[readIndex].bootCount == 0) continue;
        empty = false;
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%u,%u,%c%04X",
                 dtcHistory[readIndex].bootCount, dtcHistory[readIndex].uptimeSecs,
                 dtcHistory[readIndex].type, dtcHistory[readIndex].code);
        bleSendLine(buffer);
    }
    if (empty) bleSendLine("No data,0,N/A");
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
    BLEDevice::deinit(true);

    WiFiManager wm;
    Serial.println("[OTA] Starting WiFiManager...");
    
    // [FIX] Implement timeout to prevent endless hanging and Task WDT panics
    wm.setConfigPortalTimeout(120);
    
    if (!wm.autoConnect("MT09_OTA_Setup")) {
        Serial.println("[OTA] Failed to connect or timeout hit. Rebooting...");
        delay(1000);
        ESP.restart();
    }

    Serial.print("\n[OTA] Wi-Fi Connected! IP Address: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.setHostname("MT09_OBD_Tool");
    ArduinoOTA.setPassword("Mt09Sp2026!"); 

    ArduinoOTA.onStart([]() { Serial.println("[OTA] Start updating"); });
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Success! Rebooting..."); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("[OTA] Progress: %u%%\r", (p / (t / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) { ESP.restart(); });

    ArduinoOTA.begin();
    otaMode = true;
}

// ==========================================
// CAN / TWAI TRANSMISSION
// ==========================================
void sendOBDRequest(uint8_t mode, uint8_t pid = 0xFF, uint32_t target_id = OBD_BROADCAST_ID) {
    twai_message_t request;
    request.identifier = target_id;
    request.extd = 0;
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

void sendFlowControl(bool abort = false) {
    twai_message_t fc;
    fc.identifier = OBD_PHYSICAL_ID;
    fc.extd = 0;
    fc.data_length_code = 8;
    
    if (abort) {
        fc.data[0] = 0x32; // Flow Control - Abort
        for (int i = 1; i < 8; i++) fc.data[i] = 0xAA;
    } else {
        fc.data[0] = 0x30; // Flow Control - Continue
        fc.data[1] = 0x00; // Block Size: 0 (Send all remaining frames continuously)
        fc.data[2] = 0x14; // Separation Time: 20ms
        for (int i = 3; i < 8; i++) fc.data[i] = 0xAA;
    }

    twai_transmit(&fc, pdMS_TO_TICKS(50));
}

void executeClear() {
    sendOBDRequest(0x04, 0xFF, OBD_PHYSICAL_ID);
    lastClearTime = millis();
    clearRequested = false;
}

void checkBusHealth() {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK) {
        if (alerts & TWAI_ALERT_BUS_OFF) twai_initiate_recovery();
        if (alerts & TWAI_ALERT_BUS_RECOVERED) twai_start();
    }
}

// ==========================================
// DTC & OBD PROCESSING
// ==========================================

// [FIX] Now strictly expects a buffer starting precisely at DTC1_H
void processDTCs(const uint8_t* dtcBuffer, uint8_t numCodes) {
    bool faultMatched = false;

    for (int i = 0; i < numCodes; i++) {
        uint8_t b1 = dtcBuffer[i * 2];
        uint8_t b2 = dtcBuffer[(i * 2) + 1];
        if (b1 == 0 && b2 == 0) continue;

        char typeChar = "PCBU"[(b1 >> 6) & 0x03];
        uint16_t code = ((b1 & 0x3F) << 8) | b2;

        if (typeChar == 'P' && ((code >= 0x0130 && code <= 0x0167) || 
                               (code >= 0x0420 && code <= 0x0439) ||
                               (code >= 0x0470 && code <= 0x0480))) {
            faultMatched = true;
            logClearedDTC(typeChar, code);
        }
    }

    if (faultMatched && !clearRequested && (millis() - lastClearTime >= CLEAR_COOLDOWN)) {
        clearRequested = true;
    }
}

void parseOBDResponse(const twai_message_t &msg) {
    uint8_t pciType = (msg.data[0] & 0xF0) >> 4;

    if (pciType == 0x0) {
        uint8_t mode = msg.data[1];
        if (mode == 0x41) {
            uint8_t pid = msg.data[2];
            
            if (liveTelemetryMode) {
                if ((telemetryStep == 0 && pid == 0x0C) || (telemetryStep == 1 && pid == 0x05) ||
                    (telemetryStep == 2 && pid == 0x11) || (telemetryStep == 3 && pid == 0x0F) ||
                    (telemetryStep == 4 && pid == 0x42)) {
                    telemetryStep = (telemetryStep + 1) % 5;
                    telemetryPending = false; 
                }
            }

            if (pid == 0x0C) { // RPM
                liveRPM = ((msg.data[3] << 8) | msg.data[4]) / 4;
                if (clearRequested && liveRPM == 0) executeClear();
            }
            else if (pid == 0x05) liveTempC = msg.data[3] - 40; 
            else if (pid == 0x11) liveTPS = (msg.data[3] * 100) / 255;
            else if (pid == 0x0F) liveIAT = msg.data[3] - 40;
            else if (pid == 0x42) { 
                liveVoltage = ((msg.data[3] << 8) | msg.data[4]) / 1000.0;
                
                if (liveRPM > 800 && liveVoltage < VOLTAGE_MIN_THRESHOLD && 
                    millis() - lastVoltageAlertTime >= VOLTAGE_ALERT_COOLDOWN) {
                    lastVoltageAlertTime = millis();
                    char alert[64];
                    snprintf(alert, sizeof(alert), "⚠️ Low Voltage! %.1fV", liveVoltage);
                    bleSendLine(alert);
                }
                
                if (liveTelemetryMode) {
                    telemetryUpdated = true; 
                }
            }
            return;
        }
        if (mode == 0x43 || mode == 0x47) {
            uint8_t dataLen = msg.data[0] & 0x0F;
            if (dataLen >= 3) {
                // [FIX] Explicitly pass the number of codes and offset the buffer to DTC1
                uint8_t numCodes = msg.data[2];
                processDTCs(&msg.data[3], numCodes);
            }
        }
    }
    else if (pciType == 0x1) { // First Frame
        uint8_t mode = msg.data[2];
        if (mode == 0x43 || mode == 0x47) {
            rxTotalLength = ((msg.data[0] & 0x0F) << 8) | msg.data[1];

            if (rxTotalLength > RX_BUFFER_SIZE) {
                sendFlowControl(true); // Send Abort Frame
                return;
            }

            rxIndex = 0;
            for (int i = 2; i < 8; i++) rxBuffer[rxIndex++] = msg.data[i];

            isReceivingMultiFrame = true;
            expectedSeqNum = 1;
            multiFrameTimeout = millis();
            sendFlowControl(false);
        }
    }
    else if (pciType == 0x2) { // Consecutive Frame
        if (!isReceivingMultiFrame) return;

        uint8_t seqNum = msg.data[0] & 0x0F;
        if (seqNum != expectedSeqNum) {
            isReceivingMultiFrame = false;
            return;
        }

        for (int i = 1; i < 8; i++) {
            if (rxIndex < rxTotalLength) rxBuffer[rxIndex++] = msg.data[i];
        }

        expectedSeqNum = (expectedSeqNum + 1) & 0x0F;
        multiFrameTimeout = millis();

        if (rxIndex >= rxTotalLength) {
            isReceivingMultiFrame = false;
            // [FIX] rxBuffer[0] = Mode, rxBuffer[1] = Num Codes, rxBuffer[2] = DTC1_H
            processDTCs(&rxBuffer[2], rxBuffer[1]);
        }
    }
}

// ==========================================
// SETUP & INITIALIZATION
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. Initialize FreeRTOS Queue and BLE Task
    bleMsgQueue = xQueueCreate(BLE_MSG_QUEUE_LEN, BLE_MSG_MAX_LEN);
    // [FIX] Pin the BLE transmission task to Core 0 to prevent main loop CAN stuttering on Core 1
    xTaskCreatePinnedToCore(bleTxTask, "BLE_TX_Task", 4096, NULL, 1, NULL, 0);

    // 2. Initialize NVS (Load individually)
    preferences.begin("obd_data", false);
    currentBootCount = preferences.getUInt("boot_cnt", 0) + 1;
    preferences.putUInt("boot_cnt", currentBootCount);
    
    for(int i = 0; i < MAX_HISTORY; i++) {
        char key[10];
        snprintf(key, sizeof(key), "dtc_%d", i);
        preferences.getBytes(key, &dtcHistory[i], sizeof(DTCRecord));
    }
    historyIndex = preferences.getUChar("hist_idx", 0);

    // 3. Initialize BLE
    BLEDevice::setMTU(517); 
    BLEDevice::init("MT09_OBD_BLE");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyRxCallbacks());

    pService->start();
    pServer->getAdvertising()->start();

    // 4. Initialize CAN/TWAI Driver with Hardware Filtering
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 50; 
    g_config.tx_queue_len = 10;
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    twai_filter_config_t f_config = {
        .acceptance_code = (uint32_t)(0x7E8U << 21), 
        .acceptance_mask = (uint32_t)~(0x007U << 21), 
        .single_filter = true
    };

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK || twai_start() != ESP_OK) {
        Serial.println("[CAN] Failed to start TWAI driver!");
        return;
    }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
    if (requestOTA.exchange(false)) {
        if (liveRPM > 0 && liveTelemetryMode) bleSendLine("[ERROR] Engine is running!");
        else enterOTAMode();
    }
    
    if (otaMode) {
        ArduinoOTA.handle(); 
        delay(10);
        return; 
    }

    if (requestTelemetryToggleAlert.exchange(false)) {
        telemetryPending = false;            
        telemetryStep = 0;
        bleSendLine(liveTelemetryMode ? "[SYSTEM] Live Telemetry Started" : "[SYSTEM] Live Telemetry Stopped");
    }

    if (requestPrintHistory.exchange(false)) printDTCHistory();
    if (requestCSVExport.exchange(false)) exportHistoryCSV();

    checkBusHealth();

    if (telemetryUpdated.exchange(false) && liveTelemetryMode) {
        char tBuffer[128]; 
        snprintf(tBuffer, sizeof(tBuffer), "RPM:%04d | Coolant:%dC | IAT:%dC | TPS:%02d%% | Batt:%.1fV", 
                 liveRPM, liveTempC, liveIAT, liveTPS, liveVoltage);
        bleSendLine(tBuffer);
    }

    if (isReceivingMultiFrame && (millis() - multiFrameTimeout > MULTI_FRAME_TIMEOUT_MS)) {
        isReceivingMultiFrame = false;
        rxIndex = 0;
        rxTotalLength = 0;
    }

    twai_message_t rx_msg;
    while (twai_receive(&rx_msg, pdMS_TO_TICKS(1)) == ESP_OK) {
        lastCanRxTime = millis(); 
        parseOBDResponse(rx_msg); 
    }

    if (millis() - lastCanRxTime > 3000) {
        if (liveRPM > 0) liveRPM = 0; 
        if (clearRequested) clearRequested = false; 
    }

    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }

    if (liveTelemetryMode) {
        if (!telemetryPending || (millis() - telemetrySentTime > 250)) {
            telemetryPending = true;
            telemetrySentTime = millis();
            if (telemetryStep == 0) sendOBDRequest(0x01, 0x0C);      
            else if (telemetryStep == 1) sendOBDRequest(0x01, 0x05); 
            else if (telemetryStep == 2) sendOBDRequest(0x01, 0x11); 
            else if (telemetryStep == 3) sendOBDRequest(0x01, 0x0F); 
            else if (telemetryStep == 4) sendOBDRequest(0x01, 0x42); 
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