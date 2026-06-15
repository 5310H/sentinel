/**
 * @file zigbee_relays.h
 * @brief Zigbee virtual relay integration (relays 64-95)
 */

#ifndef ZIGBEE_RELAYS_H
#define ZIGBEE_RELAYS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set Zigbee virtual relay state
 * @param relay_id Relay ID (64-95)
 * @param on true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t zigbee_relay_set_state(int relay_id, bool on);

/**
 * @brief Get Zigbee virtual relay state
 * @param relay_id Relay ID (64-95)
 * @return Relay state (true = ON)
 */
bool zigbee_relay_get_state(int relay_id);

#ifdef __cplusplus
}
#endif

#endif // ZIGBEE_RELAYS_H