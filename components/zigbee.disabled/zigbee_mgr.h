/**
 * @file zigbee_mgr.h
 * @brief High-level Zigbee manager
 * 
 * Manages Zigbee network, device pairing, and device control.
 * Integrates with zones and relays.
 */

#ifndef ZIGBEE_MGR_H
#define ZIGBEE_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "zigbee_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event types for callbacks
 */
typedef enum {
    ZIGBEE_EVENT_DEVICE_JOINED,     // New device joined network
    ZIGBEE_EVENT_DEVICE_LEFT,       // Device left network
    ZIGBEE_EVENT_DEVICE_UPDATE,     // Device state updated
    ZIGBEE_EVENT_NETWORK_UP,        // Network started
    ZIGBEE_EVENT_NETWORK_DOWN       // Network stopped
} zigbee_event_type_t;

/**
 * @brief Event data structure
 */
typedef struct {
    zigbee_event_type_t type;
    uint16_t short_addr;
    const zigbee_device_t *device;
} zigbee_event_t;

/**
 * @brief Event callback function type
 */
typedef void (*zigbee_event_callback_t)(const zigbee_event_t *event);

/**
 * @brief Network statistics structure
 */
typedef struct {
    uint32_t total_devices;          // Total devices in registry
    uint32_t online_devices;         // Currently online
    uint32_t offline_devices;        // Currently offline
    uint32_t commands_sent;          // Total commands sent
    uint32_t commands_failed;        // Failed commands
    uint32_t frames_received;        // Frames received from coordinator
    uint32_t low_battery_devices;    // Devices with battery <20%
    uint32_t uptime_seconds;         // Manager uptime
} zigbee_network_stats_t;

/**
 * @brief Initialize Zigbee manager
 * @param config Protocol config (NULL for defaults)
 * @param callback Event callback (optional)
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_init(const void *config, zigbee_event_callback_t callback);

/**
 * @brief Deinitialize Zigbee manager
 */
void zigbee_mgr_deinit(void);

/**
 * @brief Check if Zigbee coordinator is connected
 * @return true if connected
 */
bool zigbee_mgr_is_connected(void);

/**
 * @brief Enable permit join (pairing mode)
 * @param duration_sec Duration in seconds (0 = disable, 255 = forever)
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_enable_pairing(uint16_t duration_sec);

/**
 * @brief Trigger device discovery
 * @param timeout_ms Discovery timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_discover_devices(uint32_t timeout_ms);

/**
 * @brief Get all devices
 * @param devices Output array
 * @param count Output: device count
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_get_devices(zigbee_device_t *devices, int *count);

/**
 * @brief Get device by short address
 * @param short_addr Device short address
 * @return Pointer to device or NULL
 */
const zigbee_device_t* zigbee_mgr_get_device(uint16_t short_addr);

/**
 * @brief Remove device from network
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_remove_device(uint16_t short_addr);

/**
 * @brief Set device on/off state
 * @param short_addr Device short address
 * @param on true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_on_off(uint16_t short_addr, bool on);

/**
 * @brief Set device brightness level
 * @param short_addr Device short address
 * @param level Level 0-100 (%)
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_level(uint16_t short_addr, uint8_t level);

/**
 * @brief Set device RGB color
 * @param short_addr Device short address
 * @param rgb Color in 0xRRGGBB format
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_color(uint16_t short_addr, uint32_t rgb);

/**
 * @brief Set device color temperature
 * @param short_addr Device short address
 * @param mireds Color temperature in mireds
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_color_temp(uint16_t short_addr, uint16_t mireds);

/**
 * @brief Lock/unlock device
 * @param short_addr Device short address
 * @param locked true = locked, false = unlocked
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_lock(uint16_t short_addr, bool locked);

/**
 * @brief Set thermostat temperature setpoint
 * @param short_addr Device short address
 * @param temperature Temperature in °C (e.g., 21 for 21°C)
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_thermostat(uint16_t short_addr, float temperature);

/**
 * @brief Get battery level for device
 * @param short_addr Device short address
 * @return Battery level 0-100%, or -1 if not available
 */
int zigbee_mgr_get_battery_level(uint16_t short_addr);

/**
 * @brief Check if device battery is low (<20%)
 * @param short_addr Device short address
 * @return true if battery low
 */
bool zigbee_mgr_is_battery_low(uint16_t short_addr);

/**
 * @brief Get network statistics
 * @param stats Pointer to stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_get_statistics(zigbee_network_stats_t *stats);

/**
 * @brief Map device to zone (sensors only)
 * @param short_addr Device short address
 * @param zone_id Zone ID (50-64), 0 to unmap
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_map_to_zone(uint16_t short_addr, int zone_id);

/**
 * @brief Map device to relay (outputs only)
 * @param short_addr Device short address
 * @param relay_id Relay ID (64-95), 0 to unmap
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_map_to_relay(uint16_t short_addr, int relay_id);

/**
 * @brief Get zone state for mapped sensor
 * @param zone_id Zone ID (50-64)
 * @return Zone state (true = triggered)
 */
bool zigbee_mgr_get_zone_state(int zone_id);

/**
 * @brief Set relay state for mapped output
 * @param relay_id Relay ID (64-95)
 * @param on true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t zigbee_mgr_set_relay_state(int relay_id, bool on);

#ifdef __cplusplus
}
#endif

#endif // ZIGBEE_MGR_H
