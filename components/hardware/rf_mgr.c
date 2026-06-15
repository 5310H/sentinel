#include "hal.h"
#include <stdio.h>

// Initialize the global variable
uint32_t last_rf_code = 0;

#ifdef ESP_PLATFORM
    // --- THIS SECTION ONLY RUNS ON THE REAL ESP32 ---
    #include "driver/rmt.h"
    
    void rf_init() {
        rmt_config_t rx_conf = RMT_DEFAULT_CONFIG_RX(4, RMT_CHANNEL_0);
        rx_conf.rx_config.filter_en = true;
        rmt_config(&rx_conf);
        rmt_driver_install(RMT_CHANNEL_0, 1000, 0);
        printf("[RF] ESP32 RMT Hardware Initialized\n");
    }

    void rf_send_code(uint32_t code) {
        // Real RMT transmit logic here
        printf("[RF] ESP32 Transmitting: 0x%08X\n", code);
    }
#else
    // --- THIS SECTION ONLY RUNS ON LINUX MOCK SERVER ---
    void rf_init() {
        printf("[RF-MOCK] Linux Simulator: RF initialized (Software only)\n");
    }

    void rf_send_code(uint32_t code) {
        printf("[RF-MOCK] Linux Simulator: Simulated transmission of 0x%08X\n", code);
    }
#endif