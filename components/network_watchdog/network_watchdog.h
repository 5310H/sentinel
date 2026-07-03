#ifndef NETWORK_WATCHDOG_H
#define NETWORK_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network connectivity status enumeration
 */
typedef enum {
    NET_WATCHDOG_STATE_UNKNOWN,
    NET_WATCHDOG_STATE_ONLINE,       // Internet is reachable
    NET_WATCHDOG_STATE_LOCAL_ONLY,   // Local gateway reachable, external unreachable
    NET_WATCHDOG_STATE_OFFLINE       // Nothing is reachable
} net_watchdog_state_t;

/**
 * @brief Callback function type for watchdog state changes
 * @param new_state The new network state
 */
typedef void (*net_watchdog_cb_t)(net_watchdog_state_t new_state);

/**
 * @brief Initialize and start the network watchdog task
 * 
 * @param interval_minutes How often to run the check (in minutes)
 * @param callback Callback to invoke on state change
 * @return esp_err_t ESP_OK on success
 */
esp_err_t network_watchdog_start(uint32_t interval_minutes, net_watchdog_cb_t callback);

/**
 * @brief Stop the network watchdog task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t network_watchdog_stop(void);

/**
 * @brief Get the current network state
 * 
 * @return net_watchdog_state_t Current state
 */
net_watchdog_state_t network_watchdog_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_WATCHDOG_H
