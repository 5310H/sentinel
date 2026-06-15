/**
 * @file tuya_relays.c
 * @brief Tuya Virtual Relay Integration
 * 
 * Maps Tuya output devices to virtual relays (32-63).
 * Integrates with engine relay control system.
 */

#include "tuya_mgr.h"
#include "tuya_devices.h"
#include "../engine/engine.h"
#include "../storage/storage_mgr.h"
#include "esp_log.h"

static const char *TAG = "tuya_relays";

// Virtual relay range for Tuya devices
#define TUYA_RELAY_START    32
#define TUYA_RELAY_END      63
#define TUYA_RELAY_COUNT    (TUYA_RELAY_END - TUYA_RELAY_START + 1)

/**
 * @brief Check if relay ID is in Tuya range
 */
bool tuya_is_relay_id_valid(int relay_id) {
    return (relay_id >= TUYA_RELAY_START && relay_id <= TUYA_RELAY_END);
}

/**
 * @brief Control Tuya virtual relay
 * 
 * Called by engine or API handlers to control Tuya output devices
 * mapped to virtual relays.
 */
esp_err_t tuya_relay_set_state(int relay_id, bool on) {
    if (!tuya_is_relay_id_valid(relay_id)) {
        ESP_LOGE(TAG, "Invalid relay ID: %d", relay_id);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = tuya_mgr_set_relay_state(relay_id, on);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Tuya relay %d set to %s", relay_id, on ? "ON" : "OFF");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "Tuya relay %d not mapped to any device", relay_id);
    }
    
    return ret;
}

/**
 * @brief Get Tuya virtual relay state
 * 
 * Returns the current state of a Tuya device mapped to a virtual relay.
 */
bool tuya_relay_get_state(int relay_id) {
    if (!tuya_is_relay_id_valid(relay_id)) {
        return false;
    }

    // Find device mapped to this relay
    tuya_device_t devices[TUYA_DEVICE_MAX_COUNT];
    int count;
    
    esp_err_t ret = tuya_mgr_get_devices(devices, TUYA_DEVICE_MAX_COUNT, &count);
    if (ret != ESP_OK) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (devices[i].virtual_relay_id == relay_id) {
            return devices[i].switch_on;
        }
    }
    
    return false;
}

/**
 * @brief Initialize Tuya relays
 */
esp_err_t tuya_relays_init(void) {
    ESP_LOGI(TAG, "Tuya relays initialized (%d-%d)", TUYA_RELAY_START, TUYA_RELAY_END);
    return ESP_OK;
}
