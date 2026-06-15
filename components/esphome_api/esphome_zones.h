/**
 * @file esphome_zones.h
 * @brief ESPHome Virtual Zone Integration Header
 */

#ifndef ESPHOME_ZONES_H
#define ESPHOME_ZONES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if zone ID is in ESPHome range (33-64)
 */
bool esphome_is_zone_id_valid(int zone_id);

/**
 * @brief Get zone state for ESPHome virtual zone
 */
bool esphome_zone_get_state(int zone_id);

/**
 * @brief Initialize ESPHome zone monitoring
 */
void esphome_zones_init(void);

/**
 * @brief Set ESPHome zone state (for external triggers)
 */
void esphome_zone_set_state(int zone_id, bool triggered);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_ZONES_H