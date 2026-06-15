/**
 * @file zigbee_relays.c
 * @brief Zigbee relays implementation
 */

#include "zigbee_relays.h"
#include "zigbee_mgr.h"
#include "esp_log.h"

static const char *TAG = "ZIGBEE_RELAYS";

/**
 * Set relay state
 */
esp_err_t zigbee_relay_set_state(int relay_id, bool on) {
    // Validate relay range (64-95)
    if (relay_id < 64 || relay_id > 95) {
        ESP_LOGW(TAG, "Invalid Zigbee relay ID: %d", relay_id);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Setting relay %d to %s", relay_id, on ? "ON" : "OFF");
    
    return zigbee_mgr_set_relay_state(relay_id, on);
}

/**
 * Get relay state
 */
bool zigbee_relay_get_state(int relay_id) {
    // Validate relay range (64-95)
    if (relay_id < 64 || relay_id > 95) {
        return false;
    }

    // TODO: Track relay states locally
    return false;
}