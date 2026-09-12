#include <Arduino.h>
#include "driver/twai.h"

// =====================================================
// ESP32 <-> SN65HVD230
// =====================================================

#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

// MT-09 CAN is reported as 500 kbit/s
// =====================================================

void setup() {

    Serial.begin(115200);
    delay(1500);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" MT-09 CAN BUS SNIFFER");
    Serial.println(" ESP32 + SN65HVD230");
    Serial.println("====================================");
    Serial.println();

    // -------------------------------------------------
    // CAN configuration
    // -------------------------------------------------

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(
            CAN_TX_PIN,
            CAN_RX_PIN,
            TWAI_MODE_NORMAL
        );

    // Larger queues for sniffing
    g_config.rx_queue_len = 100;
    g_config.tx_queue_len = 10;

    // We are listening only, so don't enable
    // unnecessary bus-off recovery logic yet.
    g_config.alerts_enabled =
        TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_ERR_PASS |
        TWAI_ALERT_BUS_OFF |
        TWAI_ALERT_BUS_RECOVERED;

    // -------------------------------------------------
    // 500 kbit/s
    // -------------------------------------------------

    twai_timing_config_t t_config =
        TWAI_TIMING_CONFIG_500KBITS();

    // -------------------------------------------------
    // IMPORTANT:
    // Accept EVERY CAN ID
    // -------------------------------------------------

    twai_filter_config_t f_config =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // -------------------------------------------------
    // Install driver
    // -------------------------------------------------

    esp_err_t result;

    result = twai_driver_install(
        &g_config,
        &t_config,
        &f_config
    );

    if (result != ESP_OK) {
        Serial.printf(
            "[CAN] Driver install FAILED: %d\n",
            result
        );

        while (1) {
            delay(1000);
        }
    }

    Serial.println("[CAN] Driver installed");

    // -------------------------------------------------
    // Start CAN
    // -------------------------------------------------

    result = twai_start();

    if (result != ESP_OK) {
        Serial.printf(
            "[CAN] Start FAILED: %d\n",
            result
        );

        while (1) {
            delay(1000);
        }
    }

    Serial.println("[CAN] Started");
    Serial.println("[CAN] 500 kbit/s");
    Serial.println("[CAN] Accepting ALL CAN IDs");
    Serial.println();
    Serial.println("Waiting for CAN frames...");
    Serial.println();
}


// =====================================================
// PRINT CAN FRAME
// =====================================================

void printCANFrame(const twai_message_t &msg) {

    Serial.printf(
        "ID: 0x%03lX  DLC: %d  DATA:",
        (unsigned long)msg.identifier,
        msg.data_length_code
    );

    for (int i = 0; i < msg.data_length_code; i++) {

        Serial.printf(
            " %02X",
            msg.data[i]
        );
    }

    Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

    twai_message_t message;

    // -------------------------------------------------
    // Receive CAN frame
    // -------------------------------------------------

    if (twai_receive(
            &message,
            pdMS_TO_TICKS(100)
        ) == ESP_OK) {

        printCANFrame(message);
    }

    // -------------------------------------------------
    // Check CAN status every 1 second
    // -------------------------------------------------

    static unsigned long lastStatus = 0;

    if (millis() - lastStatus >= 1000) {

        lastStatus = millis();

        twai_status_info_t status;

        if (twai_get_status_info(&status) == ESP_OK) {

            Serial.printf(
                "[STATUS] State:%d TXerr:%lu RXerr:%lu "
                "BusErr:%lu TXfailed:%lu\n",

                status.state,
                (unsigned long)status.tx_error_counter,
                (unsigned long)status.rx_error_counter,
                (unsigned long)status.bus_error_count,
                (unsigned long)status.tx_failed_count
            );
        }
    }
}