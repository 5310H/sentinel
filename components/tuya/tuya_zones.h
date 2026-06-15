/**
 * @file tuya_zones.h
 * @brief Tuya Virtual Zone Integration
 */

#ifndef TUYA_ZONES_H
#define TUYA_ZONES_H

#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if zone ID is in Tuya range (67-96)
 */
bool tuya_is_zone_id_valid(int zone_id);

/**
 * @brief Get zone state for Tuya virtual zone
 * @param zone_id Zone ID (67-96)
 * @return true if zone triggered, false otherwise
 */
bool tuya_zone_get_state(int zone_id);

/**
 * @brief Handle Tuya sensor event
 * @param device_id Device ID
 * @param triggered Sensor state
 */
#ifdef ESP_PLATFORM
void tuya_zone_handle_sensor_event(const char *device_id, bool triggered);
#endif

/**
 * @brief Initialize Tuya zones
 */
#ifdef ESP_PLATFORM
esp_err_t tuya_zones_init(void);
#else
void tuya_zones_init(void);
#endif

/**
 * @brief Set Tuya zone state (for testing on Linux)
 * @param zone_id Zone ID (67-96)
 * @param triggered Zone state
 */
#ifndef ESP_PLATFORM
void tuya_zone_set_state(int zone_id, bool triggered);
#endif

#ifdef __cplusplus
}
#endif

#endif // TUYA_ZONES_H
