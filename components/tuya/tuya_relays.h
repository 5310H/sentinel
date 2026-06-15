/**
 * @file tuya_relays.h
 * @brief Tuya Virtual Relay Integration
 */

#ifndef TUYA_RELAYS_H
#define TUYA_RELAYS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if relay ID is in Tuya range (32-63)
 */
bool tuya_is_relay_id_valid(int relay_id);

/**
 * @brief Control Tuya virtual relay
 * @param relay_id Relay ID (32-63)
 * @param on Relay state
 * @return ESP_OK on success
 */
esp_err_t tuya_relay_set_state(int relay_id, bool on);

/**
 * @brief Get Tuya virtual relay state
 * @param relay_id Relay ID (32-63)
 * @return true if relay is on
 */
bool tuya_relay_get_state(int relay_id);

/**
 * @brief Initialize Tuya relays
 */
esp_err_t tuya_relays_init(void);

#ifdef __cplusplus
}
#endif

#endif // TUYA_RELAYS_H
