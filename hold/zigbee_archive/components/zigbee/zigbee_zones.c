/**
 * @file zigbee_zones.c
 * @brief Zigbee zones implementation
 */

#include "zigbee_zones.h"
#include "zigbee_mgr.h"
#include "esp_log.h"

static const char *TAG = "ZIGBEE_ZONES";

/**
 * Get zone state
 */
bool zigbee_zone_get_state(int zone_id) {
    // Validate zone range (50-64)
    if (zone_id < 50 || zone_id > 64) {
        ESP_LOGW(TAG, "Invalid Zigbee zone ID: %d", zone_id);
        return false;
    }

    return zigbee_mgr_get_zone_state(zone_id);
}

/**
 * Handle sensor event
 */
void zigbee_zone_handle_sensor_event(int zone_id, bool triggered) {
    if (zone_id < 50 || zone_id > 64) {
        return;
    }

    ESP_LOGD(TAG, "Zone %d sensor event: %s", zone_id, triggered ? "TRIGGERED" : "CLEAR");
    
    // TODO: Trigger engine zone event
}