/**
 * @file tuya_zones.c
 * @brief Tuya Virtual Zone Integration
 * 
 * Maps Tuya sensor devices to virtual zones (65-96).
 * Integrates with engine zone monitoring system.
 */

#include "tuya_mgr.h"
#include "tuya_devices.h"
#include "../engine/engine.h"
#include "../storage/storage_mgr.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "tuya_zones";
#else
#include <stdio.h>
#define TAG "tuya_zones"
#endif

// Virtual zone range for Tuya devices
#define TUYA_ZONE_START     67
#define TUYA_ZONE_END       96
#define TUYA_ZONE_COUNT     (TUYA_ZONE_END - TUYA_ZONE_START + 1)

// Mock zone states for testing (when Tuya API not available)
#ifndef ESP_PLATFORM
static bool mock_tuya_zone_states[TUYA_ZONE_COUNT] = {false};
#endif

/**
 * @brief Check if zone ID is in Tuya range
 */
bool tuya_is_zone_id_valid(int zone_id) {
    return (zone_id >= TUYA_ZONE_START && zone_id <= TUYA_ZONE_END);
}

/**
 * @brief Get zone state for Tuya virtual zone
 * 
 * Called by engine during zone monitoring to get state
 * of Tuya sensor devices mapped to virtual zones.
 */
bool tuya_zone_get_state(int zone_id) {
    if (!tuya_is_zone_id_valid(zone_id)) {
        return false;
    }

#ifdef ESP_PLATFORM
    bool triggered = false;
    esp_err_t ret = tuya_mgr_get_zone_state(zone_id, &triggered);
    
    if (ret == ESP_OK) {
        return triggered;
    }
    
    return false;
#else
    // Mock implementation for Linux testing
    int mock_index = zone_id - TUYA_ZONE_START;
    return mock_tuya_zone_states[mock_index];
#endif
}

/**
 * @brief Set Tuya zone state (for testing/external triggers)
 */
#ifndef ESP_PLATFORM
void tuya_zone_set_state(int zone_id, bool triggered) {
    if (!tuya_is_zone_id_valid(zone_id)) {
        printf("[%s] WARNING: Invalid Tuya zone ID: %d\n", TAG, zone_id);
        return;
    }
    int mock_index = zone_id - TUYA_ZONE_START;
    mock_tuya_zone_states[mock_index] = triggered;
    printf("[%s] Tuya zone %d set to %s\n", TAG, zone_id, triggered ? "TRIGGERED" : "SECURE");

    // Find the zone index in the global zones array
    for (int i = 0; i < z_count; i++) {
        if (zones[i].id == zone_id) {
            // Trigger zone processing if state changed to triggered
            if (triggered && engine_get_arm_state() != 4) {
                engine_process_zone_trip(i);
            }
            break;
        }
    }
}
#endif

/**
 * @brief Initialize Tuya zones
 */
#ifdef ESP_PLATFORM
esp_err_t tuya_zones_init(void) {
    ESP_LOGI(TAG, "Tuya zones initialized (%d-%d)", TUYA_ZONE_START, TUYA_ZONE_END);
    return ESP_OK;
}
#else
void tuya_zones_init(void) {
    printf("[%s] Tuya zones initialized (%d-%d)\n", TAG, TUYA_ZONE_START, TUYA_ZONE_END);
    // Initialize mock states
    memset(mock_tuya_zone_states, 0, sizeof(mock_tuya_zone_states));
}
#endif
