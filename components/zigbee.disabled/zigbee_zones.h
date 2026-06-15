/**
 * @file zigbee_zones.h
 * @brief Zigbee virtual zone integration (zones 50-64)
 */

#ifndef ZIGBEE_ZONES_H
#define ZIGBEE_ZONES_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get zone state for Zigbee virtual zone
 * @param zone_id Zone ID (50-64)
 * @return Zone state (true = triggered/open)
 */
bool zigbee_zone_get_state(int zone_id);

/**
 * @brief Handle sensor event
 * @param zone_id Zone ID
 * @param triggered Sensor state
 */
void zigbee_zone_handle_sensor_event(int zone_id, bool triggered);

#ifdef __cplusplus
}
#endif

#endif // ZIGBEE_ZONES_H