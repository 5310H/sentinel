#ifndef HAL_ESP32_H
#define HAL_ESP32_H

#include <stdint.h>
#include <stdbool.h>

// Handle platform-specific types for the header
#ifdef ESP_PLATFORM
    #include "esp_netif.h"
#else
    // Mock type for Linux testing environment
    typedef void* esp_netif_t;
#endif

// We need to know what a zone_t is for hal_init_zones
// This usually comes from engine.h or a shared types header
#include "storage_mgr.h" 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the hardware abstraction layer
 */
void hal_esp32_init(void);

/**
 * @brief Configure GPIOs for the alarm zones
 */
void hal_init_zones(zone_t *zones, int count);

/**
 * @brief Read the current state of a zone GPIO
 * @return 1 for triggered/active, 0 for secure
 */
int hal_get_zone_state(int pin);

/**
 * @brief Set the state of a specific relay (0-7)
 */
void hal_set_relay(int index, int state);

/**
 * @brief Control the dedicated siren relay
 */
void hal_set_siren(int state);

/**
 * @brief Control the dedicated strobe relay
 */
void hal_set_strobe(int state);

/**
 * @brief Apply static IP configuration to a network interface
 */
void hal_net_apply_static_ip(esp_netif_t *netif);

/**
 * @brief Get the state of a relay based on its GPIO
 * (Required by your main.c error earlier)
 */
bool hal_get_relay_state(int gpio);

#ifdef __cplusplus
}
#endif

#endif // HAL_ESP32_H