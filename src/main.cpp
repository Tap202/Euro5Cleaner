/**
 * =============================================================================
 * 🏍️ Yamaha MT-09 SP Euro 5+ CAN Bus Sniffer
 * Platform: ESP32-C3 Super Mini Plus (Native USB CDC)
 * =============================================================================
 * 
 * Features:
 *  - TWAI Driver with safe LISTEN-ONLY mode (no ACKs, no bus disruption)
 *  - Interactive Serial CLI over Native USB-C CDC (115200 baud)
 *  - Dual Stream Output:
 *      1. Human-Readable Monitor (with interval delta, frequency & ASCII)
 *      2. SLCAN / Lawicel Mode (Plug-and-play for SavvyCAN, Wireshark, etc.)
 *  - Unique CAN ID Discovery & Statistics Tracker ('u' command)
 *  - On-the-fly Baud Rate Switching (500k, 250k, 1M, 125k) ('b' command)
 *  - Single ID Focus / Filter ('f' command)
 *  - Non-blocking LED Activity Indicator (GPIO 8)
 * =============================================================================
 */

#include <Arduino.h>
#include "driver/twai.h"

// =============================================================================
// HARDWARE PIN DEFINITIONS (ESP32-C3 Super Mini Plus)
// =============================================================================
#define CAN_TX_PIN         GPIO_NUM_21
#define CAN_RX_PIN         GPIO_NUM_3

// Onboard User LED on ESP32-C3 Super Mini is typically GPIO 8 (Active LOW)
#define ONBOARD_LED_PIN    8
#define LED_ACTIVE_LOW     true

// =============================================================================
// CAN BAUD RATES & MODES
// =============================================================================
enum CanBaudRate {
    BAUD_500K = 0,   // Standard for Yamaha Euro 5 / ISO 19689 OBD-II
    BAUD_250K = 1,   // Sub-bus / legacy OBD
    BAUD_1M   = 2,   // High-speed powertrain CAN
    BAUD_125K = 3,   // Low-speed body / diagnostic
    BAUD_COUNT = 4
};

const char* BAUD_NAMES[BAUD_COUNT] = {
    "500 kbps (Euro 5 Standard)",
    "250 kbps",
    "1000 kbps (1 Mbps)",
    "125 kbps"
};

CanBaudRate currentBaud = BAUD_500K;
bool listenOnlyMode = true; // Safe passive sniffing by default (no dominant bits/ACKs sent)
bool twaiInstalled = false;
bool twaiRunning = false;

// =============================================================================
// OUTPUT MODES & STREAM CONTROLS
// =============================================================================
enum OutputMode {
    OUTPUT_HUMAN = 0, // Formatted text for terminal / Serial Monitor
    OUTPUT_SLCAN = 1  // Lawicel ASCII protocol for SavvyCAN / Wireshark
};

OutputMode currentOutputMode = OUTPUT_HUMAN;
bool streamPaused = false;
uint32_t filterId = 0;       // 0 = no filter (all IDs allowed)
bool filterActive = false;

// =============================================================================
// UNIQUE CAN ID TRACKER
// =============================================================================
#define MAX_UNIQUE_IDS 96

struct CanIdStats {
    uint32_t id;
    bool isExtended;
    uint32_t count;
    uint32_t lastSeenMs;
    uint32_t lastIntervalMs;
    uint8_t dlc;
    uint8_t lastData[8];
};

CanIdStats uniqueIds[MAX_UNIQUE_IDS];
uint16_t uniqueIdCount = 0;

// Bus Statistics
uint32_t totalFramesRx = 0;
uint32_t framesLastSecond = 0;
uint32_t currentFps = 0;
unsigned long lastFpsCalcMs = 0;
uint32_t busErrorCount = 0;
uint32_t rxOverrunCount = 0;

// LED Blink state
unsigned long ledTurnOffMs = 0;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
bool initTWAI(CanBaudRate baud, bool listenOnly);
void stopTWAI();
void printBanner();
void printHelp();
void printStats();
void printUniqueIdsTable();
void processSerialInput();
void handleFrame(const twai_message_t &rxMsg);
void triggerLedActivity();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // Initialize USB CDC Serial
    Serial.begin(115200);

    // Initialize Onboard LED
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW); // OFF

    // Wait briefly for native USB CDC connection (up to 2.5s)
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 2500)) {
        delay(10);
    }
    delay(200);

    printBanner();

    // Start TWAI in safe LISTEN-ONLY mode at 500 kbps
    if (initTWAI(currentBaud, listenOnlyMode)) {
        Serial.printf("[INIT] CAN Controller initialized successfully on TX: GPIO %d, RX: GPIO %d\n", CAN_TX_PIN, CAN_RX_PIN);
        Serial.printf("[INIT] Speed: %s | Mode: %s\n", BAUD_NAMES[currentBaud], listenOnlyMode ? "LISTEN-ONLY (Safe Passive)" : "NORMAL (Active ACK)");
        Serial.println("[INIT] Listening for traffic... Press '?' or 'h' for menu.\n");
    } else {
        Serial.println("[ERROR] Failed to start CAN Controller! Check pins and wiring.");
    }
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    // 1. Process incoming commands from USB Serial
    if (Serial.available()) {
        processSerialInput();
    }

    // 2. Receive CAN messages from TWAI hardware
    if (twaiRunning) {
        twai_message_t rxMsg;
        // Non-blocking read (0 timeout)
        while (twai_receive(&rxMsg, 0) == ESP_OK) {
            handleFrame(rxMsg);
        }

        // Check for alerts / errors
        uint32_t alertsTriggered = 0;
        if (twai_read_alerts(&alertsTriggered, 0) == ESP_OK) {
            if (alertsTriggered & TWAI_ALERT_BUS_ERROR) {
                busErrorCount++;
            }
            if (alertsTriggered & TWAI_ALERT_RX_QUEUE_FULL) {
                rxOverrunCount++;
            }
            if (alertsTriggered & TWAI_ALERT_BUS_OFF) {
                if (currentOutputMode == OUTPUT_HUMAN) {
                    Serial.println("\n[ALERT] TWAI Bus-Off detected! Initiating recovery...");
                }
                twai_initiate_recovery();
            }
            if (alertsTriggered & TWAI_ALERT_BUS_RECOVERED) {
                if (currentOutputMode == OUTPUT_HUMAN) {
                    Serial.println("\n[ALERT] TWAI Bus Recovered!");
                }
                twai_start();
            }
        }
    }

    // 3. Update FPS counter every second
    unsigned long now = millis();
    if (now - lastFpsCalcMs >= 1000) {
        currentFps = framesLastSecond;
        framesLastSecond = 0;
        lastFpsCalcMs = now;
    }

    // 4. Handle Non-blocking Activity LED Turn-off
    if (ledTurnOffMs > 0 && now >= ledTurnOffMs) {
        digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW); // OFF
        ledTurnOffMs = 0;
    }
}

// =============================================================================
// TWAI CONTROLLER MANAGEMENT
// =============================================================================
bool initTWAI(CanBaudRate baud, bool listenOnly) {
    if (twaiRunning) {
        stopTWAI();
    }

    // 1. General Configuration
    twai_general_config_t g_config;
    if (listenOnly) {
        g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    } else {
        g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    }
    g_config.rx_queue_len = 64; // Large RX buffer to prevent drops during bursts
    g_config.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL | 
                             TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

    // 2. Timing Configuration
    twai_timing_config_t t_config;
    switch (baud) {
        case BAUD_500K:
            t_config = TWAI_TIMING_CONFIG_500KBITS();
            break;
        case BAUD_250K:
            t_config = TWAI_TIMING_CONFIG_250KBITS();
            break;
        case BAUD_1M:
            t_config = TWAI_TIMING_CONFIG_1MBITS();
            break;
        case BAUD_125K:
            t_config = TWAI_TIMING_CONFIG_125KBITS();
            break;
        default:
            t_config = TWAI_TIMING_CONFIG_500KBITS();
            break;
    }

    // 3. Filter Configuration (Accept all frames)
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install Driver
    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        return false;
    }
    twaiInstalled = true;

    // Start Driver
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

// =============================================================================
// FRAME PROCESSING & OUTPUT
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

    // Check Filter if active
    if (filterActive && id != filterId) {
        return;
    }

    // 1. Update Unique ID Stats
    uint32_t intervalMs = 0;
    int foundIdx = -1;
    for (uint16_t i = 0; i < uniqueIdCount; i++) {
        if (uniqueIds[i].id == id && uniqueIds[i].isExtended == isExt) {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx >= 0) {
        uniqueIds[foundIdx].count++;
        intervalMs = now - uniqueIds[foundIdx].lastSeenMs;
        uniqueIds[foundIdx].lastIntervalMs = intervalMs;
        uniqueIds[foundIdx].lastSeenMs = now;
        uniqueIds[foundIdx].dlc = dlc;
        if (!isRtr) {
            memcpy(uniqueIds[foundIdx].lastData, rxMsg.data, (dlc > 8) ? 8 : dlc);
        }
    } else if (uniqueIdCount < MAX_UNIQUE_IDS) {
        uniqueIds[uniqueIdCount].id = id;
        uniqueIds[uniqueIdCount].isExtended = isExt;
        uniqueIds[uniqueIdCount].count = 1;
        uniqueIds[uniqueIdCount].lastSeenMs = now;
        uniqueIds[uniqueIdCount].lastIntervalMs = 0;
        uniqueIds[uniqueIdCount].dlc = dlc;
        if (!isRtr) {
            memcpy(uniqueIds[uniqueIdCount].lastData, rxMsg.data, (dlc > 8) ? 8 : dlc);
        }
        uniqueIdCount++;
    }

    if (streamPaused) {
        return;
    }

    // 2. Output Formatting
    if (currentOutputMode == OUTPUT_HUMAN) {
        // Human Readable Format:
        // [   12.345s] ID: 0x7E8  [STD] DLC: 8  DATA: 02 01 00 00 00 00 00 00 | ........ | (Δ 20ms | 50.0 Hz)
        float sec = now / 1000.0;
        Serial.printf("[%8.3fs] ID: 0x", sec);
        if (isExt) {
            Serial.printf("%08X [EXT]", id);
        } else {
            Serial.printf("%03X      [STD]", id);
        }

        if (isRtr) {
            Serial.printf(" DLC: %d [RTR FRAME]\n", dlc);
            return;
        }

        Serial.printf(" DLC: %d  DATA: ", dlc);

        // Hex data bytes
        for (int i = 0; i < 8; i++) {
            if (i < dlc) {
                Serial.printf("%02X ", rxMsg.data[i]);
            } else {
                Serial.print("   ");
            }
        }

        // ASCII representation
        Serial.print("| ");
        for (int i = 0; i < dlc; i++) {
            char c = (char)rxMsg.data[i];
            if (c >= 32 && c <= 126) {
                Serial.print(c);
            } else {
                Serial.print('.');
            }
        }
        for (int i = dlc; i < 8; i++) {
            Serial.print(' ');
        }
        Serial.print(" |");

        // Delta time / frequency
        if (intervalMs > 0) {
            float hz = 1000.0f / (float)intervalMs;
            Serial.printf(" (Δ %ums | %4.1fHz)", intervalMs, hz);
        }
        Serial.println();

    } else if (currentOutputMode == OUTPUT_SLCAN) {
        // Lawicel SLCAN format for SavvyCAN / Wireshark:
        // Standard frame: t<id 3 hex><dlc 1 hex><data hex>\r
        // Extended frame: T<id 8 hex><dlc 1 hex><data hex>\r
        // Remote standard: r<id 3 hex><dlc 1 hex>\r
        // Remote extended: R<id 8 hex><dlc 1 hex>\r
        if (isRtr) {
            if (isExt) {
                Serial.printf("R%08X%1X\r", id, dlc);
            } else {
                Serial.printf("r%03X%1X\r", id, dlc);
            }
        } else {
            if (isExt) {
                Serial.printf("T%08X%1X", id, dlc);
            } else {
                Serial.printf("t%03X%1X", id, dlc);
            }
            for (int i = 0; i < dlc && i < 8; i++) {
                Serial.printf("%02X", rxMsg.data[i]);
            }
            Serial.print("\r");
        }
    }
}

void triggerLedActivity() {
    digitalWrite(ONBOARD_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH); // Turn ON
    ledTurnOffMs = millis() + 15; // Stay on for 15ms
}

// =============================================================================
// SERIAL INTERACTIVE CLI
// =============================================================================
void processSerialInput() {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    char cmd = input.charAt(0);

    switch (cmd) {
        case '?':
        case 'h':
        case 'H':
            printHelp();
            break;

        case 'm':
        case 'M':
            if (currentOutputMode == OUTPUT_HUMAN) {
                currentOutputMode = OUTPUT_SLCAN;
                Serial.println("\n[MODE] Switched to SLCAN / Lawicel protocol (for SavvyCAN / Wireshark).");
            } else {
                currentOutputMode = OUTPUT_HUMAN;
                Serial.println("\n[MODE] Switched to Human-Readable Monitor.");
            }
            break;

        case 'l':
        case 'L':
            listenOnlyMode = !listenOnlyMode;
            Serial.printf("\n[CONFIG] Switching to %s mode...\n", listenOnlyMode ? "LISTEN-ONLY (Safe Passive)" : "NORMAL (Active ACK)");
            if (initTWAI(currentBaud, listenOnlyMode)) {
                Serial.println("[CONFIG] Mode applied successfully.");
            } else {
                Serial.println("[ERROR] Failed to switch mode!");
            }
            break;

        case 'b':
        case 'B': {
            currentBaud = (CanBaudRate)((currentBaud + 1) % BAUD_COUNT);
            Serial.printf("\n[CONFIG] Changing Baud Rate to: %s\n", BAUD_NAMES[currentBaud]);
            if (initTWAI(currentBaud, listenOnlyMode)) {
                Serial.println("[CONFIG] Baud rate applied successfully.");
            } else {
                Serial.println("[ERROR] Failed to apply baud rate!");
            }
            break;
        }

        case 'u':
        case 'U':
            printUniqueIdsTable();
            break;

        case 's':
        case 'S':
            printStats();
            break;

        case 'p':
        case 'P':
            streamPaused = !streamPaused;
            Serial.printf("\n[STREAM] %s\n", streamPaused ? "PAUSED (Frame stats still accumulating)" : "RESUMED");
            break;

        case 'c':
        case 'C':
            uniqueIdCount = 0;
            totalFramesRx = 0;
            busErrorCount = 0;
            rxOverrunCount = 0;
            filterActive = false;
            filterId = 0;
            Serial.print("\033[2J\033[H"); // ANSI Clear Screen
            Serial.println("\n[RESET] Cleared all counters, unique IDs, and filters.");
            break;

        case 'f':
        case 'F': {
            // Filter command: "f 7E8" or "f" to clear
            if (input.length() > 2) {
                String hexStr = input.substring(1);
                hexStr.trim();
                uint32_t parsedId = strtoul(hexStr.c_str(), NULL, 16);
                if (parsedId > 0) {
                    filterId = parsedId;
                    filterActive = true;
                    Serial.printf("\n[FILTER] Now focusing only on CAN ID: 0x%X\n", filterId);
                } else {
                    filterActive = false;
                    Serial.println("\n[FILTER] Filter cleared. Showing all IDs.");
                }
            } else {
                filterActive = false;
                Serial.println("\n[FILTER] Filter cleared. Showing all IDs.");
            }
            break;
        }

        default:
            Serial.printf("[CLI] Unknown command '%c'. Type '?' or 'h' for help.\n", cmd);
            break;
    }
}

// =============================================================================
// CLI REPORTS & MENUS
// =============================================================================
void printBanner() {
    Serial.println("\n=================================================================");
    Serial.println("  🏍️  YAMAHA MT-09 SP EURO 5+ CAN BUS SNIFFER");
    Serial.println("  Hardware: ESP32-C3 Super Mini Plus | TWAI CAN Controller");
    Serial.println("=================================================================");
}

void printHelp() {
    Serial.println("\n-----------------------------------------------------------------");
    Serial.println("                    INTERACTIVE SERIAL COMMANDS                  ");
    Serial.println("-----------------------------------------------------------------");
    Serial.println("  ? or h       : Show this help menu");
    Serial.println("  m            : Toggle Output Mode (Human-Readable vs SLCAN for SavvyCAN)");
    Serial.println("  l            : Toggle Mode (Listen-Only [Passive] vs Normal [Active ACK])");
    Serial.println("  b            : Cycle Baud Rate (500k -> 250k -> 1M -> 125k)");
    Serial.println("  u            : Display Discovered Unique CAN IDs Table");
    Serial.println("  s            : Display CAN Bus Health & Diagnostic Statistics");
    Serial.println("  p            : Pause / Resume live frame streaming");
    Serial.println("  f <hex_id>   : Filter by CAN ID (e.g. 'f 7E8' or 'f' to clear)");
    Serial.println("  c            : Clear screen, counters, and unique ID table");
    Serial.println("-----------------------------------------------------------------\n");
}

void printStats() {
    twai_status_info_t status;
    twai_get_status_info(&status);

    const char* stateStr = "UNKNOWN";
    switch (status.state) {
        case TWAI_STATE_STOPPED:    stateStr = "STOPPED"; break;
        case TWAI_STATE_RUNNING:    stateStr = "RUNNING (Active)"; break;
        case TWAI_STATE_BUS_OFF:    stateStr = "BUS-OFF (Check physical wiring / baud)"; break;
        case TWAI_STATE_RECOVERING: stateStr = "RECOVERING"; break;
    }

    Serial.println("\n=================================================================");
    Serial.println("                     CAN BUS DIAGNOSTICS & HEALTH                ");
    Serial.println("=================================================================");
    Serial.printf("  Hardware State      : %s\n", stateStr);
    Serial.printf("  Operating Mode      : %s\n", listenOnlyMode ? "LISTEN-ONLY (Safe Passive)" : "NORMAL (Active ACK)");
    Serial.printf("  Baud Rate           : %s\n", BAUD_NAMES[currentBaud]);
    Serial.printf("  CAN Pins            : TX: GPIO %d | RX: GPIO %d\n", CAN_TX_PIN, CAN_RX_PIN);
    Serial.printf("  Total Frames Rx     : %u frames\n", totalFramesRx);
    Serial.printf("  Current Bus Load    : %u frames/sec (FPS)\n", currentFps);
    Serial.printf("  Unique IDs Seen     : %u / %d\n", uniqueIdCount, MAX_UNIQUE_IDS);
    Serial.printf("  Bus Error Warnings  : %u\n", busErrorCount);
    Serial.printf("  RX Queue Overruns   : %u\n", rxOverrunCount);
    Serial.printf("  Hardware TEC / REC  : %u / %u (Transmit / Receive Error Counters)\n", status.tx_error_counter, status.rx_error_counter);
    if (filterActive) {
        Serial.printf("  Active ID Filter    : 0x%X\n", filterId);
    } else {
        Serial.println("  Active ID Filter    : NONE (All frames streamed)");
    }
    Serial.println("=================================================================\n");
}

void printUniqueIdsTable() {
    Serial.println("\n============================================================================================");
    Serial.println("                                DISCOVERED UNIQUE CAN IDs TABLE                             ");
    Serial.println("============================================================================================");
    Serial.println(" ID          Type  DLC   Count      Rate (Hz)   Interval   Last Data Payload (Hex)          ");
    Serial.println("--------------------------------------------------------------------------------------------");

    if (uniqueIdCount == 0) {
        Serial.println("  (No CAN traffic received yet. Check bike ignition, transceiver wiring, and baud rate)");
    } else {
        for (uint16_t i = 0; i < uniqueIdCount; i++) {
            if (uniqueIds[i].isExtended) {
                Serial.printf(" 0x%08X  EXT   %d   %-9u", uniqueIds[i].id, uniqueIds[i].dlc, uniqueIds[i].count);
            } else {
                Serial.printf(" 0x%03X       STD   %d   %-9u", uniqueIds[i].id, uniqueIds[i].dlc, uniqueIds[i].count);
            }

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
    Serial.println("--------------------------------------------------------------------------------------------");
    Serial.printf(" Total Unique IDs: %u | Total Rx: %u frames | Speed: %s\n", uniqueIdCount, totalFramesRx, BAUD_NAMES[currentBaud]);
    Serial.println("============================================================================================\n");
}