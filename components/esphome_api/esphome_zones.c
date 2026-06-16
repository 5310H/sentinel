/**
 * @file esphome_zones.c
 * @brief ESPHome Virtual Zone Integration
 *
 * Maps ESPHome sensor devices to virtual zones (33-64).
 * Integrates with engine zone monitoring system.
 */

#include <stdio.h>
#include "esphome_zones.h"
#include "../engine/engine.h"
#include "../storage/storage_mgr.h"
#include "esphome_api_client.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "esphome_zones";
#else
#define TAG "esphome_zones"
#endif

// ESPHome virtual zone range
#define ESPHOME_ZONE_START     33
#define ESPHOME_ZONE_END       64
#define ESPHOME_ZONE_COUNT     (ESPHOME_ZONE_END - ESPHOME_ZONE_START + 1)

// Mock zone states for testing (when ESPHome API not available)
static bool mock_zone_states[ESPHOME_ZONE_COUNT] = {false};

/**
 * @brief Check if zone ID is in ESPHome range
 */
bool esphome_is_zone_id_valid(int zone_id) {
    return (zone_id >= ESPHOME_ZONE_START && zone_id <= ESPHOME_ZONE_END);
}

/**
 * @brief Get zone state for ESPHome virtual zone
 *
 * Called by engine during zone monitoring to get state
 * of ESPHome sensor devices mapped to virtual zones.
 */
bool esphome_zone_get_state(int zone_id) {
    if (!esphome_is_zone_id_valid(zone_id)) {
        return false;
    }

    // Check if any ESPHome device has this zone mapped and is enabled
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->virtual_zone_start == zone_id && storage_get_esphome_device(i)->enabled) {
#ifdef ESP_PLATFORM
            // TODO: Check actual device state via ESPHome API
            // For now, use mock state
            int mock_index = zone_id - ESPHOME_ZONE_START;
            return mock_zone_states[mock_index];
#else
            // On Linux, try to connect to real ESPHome device
            if (!storage_get_esphome_device(i)->is_connected) {
                // Attempt to connect
                if (esphome_api_connect(storage_get_esphome_device(i)->hostname, storage_get_esphome_device(i)->port,
                                       storage_get_esphome_device(i)->password, 
                                       storage_get_esphome_device(i)->encryption_key) == 0) {
                    printf("[%s] Connected to ESPHome device %s for zone monitoring\n", 
                           TAG, storage_get_esphome_device(i)->hostname);
                } else {
                    printf("[%s] Failed to connect to ESPHome device %s\n", 
                           TAG, storage_get_esphome_device(i)->hostname);
                    return false;
                }
            }
            
            // Get entity state for this zone
            esphome_entity_t entities[10];
            int entity_count = 0;
            if (esphome_api_get_entities(storage_get_esphome_device(i)->hostname, entities, 10, &entity_count) == 0) {
                // Find the binary sensor entity that corresponds to this zone
                int entity_index = zone_id - storage_get_esphome_device(i)->virtual_zone_start;
                if (entity_index >= 0 && entity_index < entity_count && 
                    entities[entity_index].type == ESPHOME_ENTITY_BINARY_SENSOR) {
                    return entities[entity_index].current_state;
                }
            }
            
            // Fallback to mock state if API fails
            int mock_index = zone_id - ESPHOME_ZONE_START;
            return mock_zone_states[mock_index];
#endif
        }
    }

    return false;
}

/**
 * @brief Initialize ESPHome zone monitoring
 */
void esphome_zones_init(void) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "ESPHome virtual zones initialized (range: %d-%d)",
             ESPHOME_ZONE_START, ESPHOME_ZONE_END);
#else
    printf("[%s] ESPHome virtual zones initialized (range: %d-%d)\n",
           TAG, ESPHOME_ZONE_START, ESPHOME_ZONE_END);
    
    // Initialize ESPHome API client for Linux (allows testing with real devices)
    if (esphome_api_init() != 0) {
        printf("[%s] Failed to initialize ESPHome API client\n", TAG);
    }
#endif

    // Initialize mock states
    memset(mock_zone_states, 0, sizeof(mock_zone_states));
}

/**
 * @brief Set ESPHome zone state (for testing/external triggers)
 */
void esphome_zone_set_state(int zone_id, bool triggered) {
    if (!esphome_is_zone_id_valid(zone_id)) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Invalid ESPHome zone ID: %d", zone_id);
#else
        printf("[%s] WARNING: Invalid ESPHome zone ID: %d\n", TAG, zone_id);
#endif
        return;
    }
    int mock_index = zone_id - ESPHOME_ZONE_START;
    mock_zone_states[mock_index] = triggered;
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "ESPHome zone %d set to %s", zone_id, triggered ? "TRIGGERED" : "SECURE");
#else
    printf("[%s] ESPHome zone %d set to %s\n", TAG, zone_id, triggered ? "TRIGGERED" : "SECURE");
#endif

    // Find the zone index in the global zones array
    for (int i = 0; i < storage_get_zone_count(); i++) {
        if (storage_get_zone(i)->id == zone_id) {
            // Trigger zone processing if state changed to triggered
            if (triggered && engine_get_arm_state() != 4) {
                engine_process_zone_trip(i);
            }
            break;
        }
    }
}