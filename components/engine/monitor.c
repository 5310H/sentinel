#include <stdio.h>
#include "monitor.h"
#include "engine.h"
#include "storage_mgr.h"
#include "camera_zones.h"

#include "../tuya/tuya_zones.h"
#include "../esphome_api/esphome_zones.h"

#ifdef ESP_PLATFORM
    #include "driver/gpio.h"
    #include "esp_log.h"
    static const char *TAG = "MONITOR";
#else
    #define TAG "MONITOR"
#endif

#define DEBOUNCE_THRESHOLD 3

// --- INITIALIZATION ---

void monitor_init(void) {
    // On ESP32, we assume engine_init has already configured the GPIO directions
    printf("[%s] Service initialized. Scanning %d zones.\n", TAG, z_count);
}

// --- EVENT PROCESSING ---

void monitor_process_event(int index) {
    if (index < 0 || index >= z_count) return;

    // Direct hand-off to the engine. 
    // The engine's tick logic will handle delays (Entry/Exit) 
    // or immediate triggers (Fire/Panic).
    engine_process_zone_trip(index); 
}

// --- HARDWARE SCANNING ---

void monitor_scan_all() {
    // 1. Scan GPIO-based zones
    for (int i = 0; i < z_count; i++) {
        // Skip virtual zones (gpio = -1) - handled separately
        if (zones[i].gpio == -1) continue;

        int state = 1; // Default to secure (High)

#ifdef ESP_PLATFORM
        state = gpio_get_level((gpio_num_t)zones[i].gpio);
#else
        // Forward to the mock digitalRead in your test environment
        extern int digitalRead(int pin);
        state = digitalRead(zones[i].gpio);
#endif

        // If state is 0, the circuit is closed (sensor triggered)
        if (state == 0) {
            // We only process if the engine isn't already in an ALARMED state
            // to prevent redundant trigger logs.
            if (engine_get_arm_state() != 4) {
                monitor_process_event(i);
            }
        }
    }

    // 2. Scan virtual zones (ESPHome, Tuya, etc.)
    for (int i = 0; i < z_count; i++) {
        // Only process virtual zones (gpio = -1)
        if (zones[i].gpio != -1) continue;

        bool triggered = false;

        // Check ESPHome virtual zones (33-64)
        if (esphome_is_zone_id_valid(zones[i].id)) {
            triggered = esphome_zone_get_state(zones[i].id);
        }
        // Check Tuya virtual zones (65-96)
        else if (tuya_is_zone_id_valid(zones[i].id)) {
            triggered = tuya_zone_get_state(zones[i].id);
        }

        // Add other virtual zone types here (Matter, Zigbee, etc.)

        // If virtual zone is triggered
        if (triggered) {
            // We only process if the engine isn't already in an ALARMED state
            // to prevent redundant trigger logs.
            if (engine_get_arm_state() != 4) {
                monitor_process_event(i);
            }
        }
    }

    // Poll camera zones for motion detection
    camera_zones_poll_all();
}

// --- ESP32 TASK WRAPPER ---
#ifdef ESP_PLATFORM
void monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Monitor Task Started on Core 1");
    while(1) {
        monitor_scan_all();
        // Scan every 50ms for high responsiveness
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif