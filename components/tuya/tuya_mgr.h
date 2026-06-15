/**
 * @file tuya_mgr.h
 * @brief Tuya Manager - Public API for Tuya Device Integration
 */

#ifndef TUYA_MGR_H
#define TUYA_MGR_H

#include <stdint.h>
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif
#include "tuya_devices.h"
#include "tuya_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tuya event types
 */
typedef enum {
    TUYA_EVENT_DEVICE_ADDED,        // New device discovered
    TUYA_EVENT_DEVICE_REMOVED,      // Device removed
    TUYA_EVENT_DEVICE_STATE,        // Device state changed
    TUYA_EVENT_SENSOR_TRIGGERED,    // Sensor triggered (zone event)
    TUYA_EVENT_CONNECTION_LOST,     // Lost connection to Tuya module
    TUYA_EVENT_CONNECTION_RESTORED, // Connection restored
} tuya_event_type_t;

/**
 * @brief Tuya event callback
 */
typedef void (*tuya_event_callback_t)(tuya_event_type_t event, const char *device_id, void *user_data);

/**
 * @brief Initialize Tuya manager
 * @param callback Event callback function (optional)
 * @param user_data User data passed to callback
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_init(tuya_event_callback_t callback, void *user_data);

/**
 * @brief Deinitialize Tuya manager
 */
void tuya_mgr_deinit(void);

/**
 * @brief Check if Tuya module is connected and responding
 * @return true if connected
 */
bool tuya_mgr_is_connected(void);

/**
 * @brief Discover Tuya devices on network
 * @param timeout_ms Discovery timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_discover_devices(uint32_t timeout_ms);

/**
 * @brief Enable pairing mode for new devices
 * @param duration_sec Pairing mode duration in seconds (default: 60)
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_enable_pairing(uint16_t duration_sec);

/**
 * @brief Get all Tuya devices
 * @param devices Output array
 * @param max Maximum devices to return
 * @param count Pointer to store actual count
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_get_devices(tuya_device_t *devices, int max, int *count);

/**
 * @brief Get device by ID
 * @param device_id Device ID
 * @param device Output device structure
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t tuya_mgr_get_device(const char *device_id, tuya_device_t *device);

/**
 * @brief Remove device
 * @param device_id Device ID
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_remove_device(const char *device_id);

/**
 * @brief Control switch device
 * @param device_id Device ID
 * @param on Switch state (true=ON, false=OFF)
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_set_switch(const char *device_id, bool on);

/**
 * @brief Control light brightness
 * @param device_id Device ID
 * @param brightness Brightness level (0-100)
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_set_brightness(const char *device_id, uint8_t brightness);

/**
 * @brief Control RGB light color
 * @param device_id Device ID
 * @param rgb RGB color (0xRRGGBB)
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_set_color(const char *device_id, uint32_t rgb);

/**
 * @brief Control color temperature
 * @param device_id Device ID
 * @param mireds Color temperature in mireds
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_set_color_temp(const char *device_id, uint16_t mireds);

/**
 * @brief Control lock
 * @param device_id Device ID
 * @param lock Lock state (true=locked, false=unlocked)
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_set_lock(const char *device_id, bool lock);

/**
 * @brief Map sensor to virtual zone
 * @param device_id Device ID
 * @param zone_id Virtual zone ID (67-96), -1 to unmap
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_map_to_zone(const char *device_id, int zone_id);

/**
 * @brief Map output device to virtual relay
 * @param device_id Device ID
 * @param relay_id Virtual relay ID (32-63), -1 to unmap
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_map_to_relay(const char *device_id, int relay_id);

/**
 * @brief Get virtual zone state for Tuya sensor
 * @param zone_id Virtual zone ID (67-96)
 * @param triggered Pointer to store zone state
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if zone not mapped
 */
esp_err_t tuya_mgr_get_zone_state(int zone_id, bool *triggered);

/**
 * @brief Control virtual relay (triggers Tuya output device)
 * @param relay_id Virtual relay ID (32-63)
 * @param on Relay state
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if relay not mapped
 */
esp_err_t tuya_mgr_set_relay_state(int relay_id, bool on);

/**
 * @brief Save configuration to storage
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_save_config(void);

/**
 * @brief Load configuration from storage
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_load_config(void);

/**
 * @brief Reset WiFi configuration on Tuya module
 * @return ESP_OK on success
 */
esp_err_t tuya_mgr_reset_wifi(void);

#ifdef __cplusplus
}
#endif

#endif // TUYA_MGR_H
