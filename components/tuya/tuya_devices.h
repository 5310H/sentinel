/**
 * @file tuya_devices.h
 * @brief Tuya Device Type Definitions and Management
 */

#ifndef TUYA_DEVICES_H
#define TUYA_DEVICES_H

#include <stdint.h>
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_DEVICE_ID_MAX_LEN      32
#define TUYA_DEVICE_NAME_MAX_LEN    64
#define TUYA_DEVICE_MAX_COUNT       32
#define TUYA_DEVICE_MAX_DP          16

// Device Types
typedef enum {
    TUYA_DEV_TYPE_UNKNOWN       = 0,
    TUYA_DEV_TYPE_SWITCH        = 1,   // On/Off switch
    TUYA_DEV_TYPE_LIGHT         = 2,   // Dimmable light
    TUYA_DEV_TYPE_RGB_LIGHT     = 3,   // RGB color light
    TUYA_DEV_TYPE_DOOR_SENSOR   = 4,   // Door/window contact sensor
    TUYA_DEV_TYPE_MOTION_SENSOR = 5,   // PIR motion sensor
    TUYA_DEV_TYPE_TEMP_SENSOR   = 6,   // Temperature sensor
    TUYA_DEV_TYPE_LOCK          = 7,   // Smart lock
    TUYA_DEV_TYPE_CURTAIN       = 8,   // Curtain motor
    TUYA_DEV_TYPE_SOCKET        = 9,   // Smart socket/outlet
    TUYA_DEV_TYPE_THERMOSTAT    = 10,  // Thermostat
} tuya_device_type_t;

// Device State
typedef enum {
    TUYA_DEV_STATE_OFFLINE      = 0,
    TUYA_DEV_STATE_ONLINE       = 1,
    TUYA_DEV_STATE_PAIRING      = 2,
    TUYA_DEV_STATE_ERROR        = 3,
} tuya_device_state_t;

// Device Capability Flags
typedef enum {
    TUYA_CAP_SWITCH         = (1 << 0),  // On/Off
    TUYA_CAP_BRIGHTNESS     = (1 << 1),  // Dimming
    TUYA_CAP_COLOR          = (1 << 2),  // RGB color
    TUYA_CAP_COLOR_TEMP     = (1 << 3),  // Color temperature
    TUYA_CAP_CONTACT        = (1 << 4),  // Contact sensor
    TUYA_CAP_MOTION         = (1 << 5),  // Motion detection
    TUYA_CAP_TEMPERATURE    = (1 << 6),  // Temperature reading
    TUYA_CAP_HUMIDITY       = (1 << 7),  // Humidity reading
    TUYA_CAP_LOCK           = (1 << 8),  // Lock/unlock
    TUYA_CAP_POSITION       = (1 << 9),  // Position (curtain)
    TUYA_CAP_BATTERY        = (1 << 10), // Battery level
} tuya_capability_t;

// Data Point Mapping
typedef struct {
    uint8_t dp_id;              // Tuya DP ID
    const char *name;           // Human-readable name
    uint8_t dp_type;            // Data point type
} tuya_dp_mapping_t;

// Device Structure
typedef struct {
    char device_id[TUYA_DEVICE_ID_MAX_LEN];     // Tuya device ID
    char name[TUYA_DEVICE_NAME_MAX_LEN];        // User-friendly name
    char description[128];                       // Zone/relay description
    char location[128];                          // Zone/relay location
    tuya_device_type_t type;                    // Device type
    tuya_device_state_t state;                  // Current state
    uint32_t capabilities;                       // Capability flags (OR of tuya_capability_t)
    
    // Zone/Relay Mapping
    int virtual_zone_id;        // Virtual zone (67-96) for sensors, -1 if not mapped
    int virtual_relay_id;       // Virtual relay (32-63) for outputs, -1 if not mapped
    
    // Current Values
    bool switch_on;             // Switch state
    uint8_t brightness;         // Brightness level (0-100)
    uint16_t color_temp;        // Color temperature (mireds)
    uint32_t rgb_color;         // RGB color (0xRRGGBB)
    bool contact_open;          // Door/window sensor (true=open)
    bool motion_detected;       // Motion sensor
    int16_t temperature;        // Temperature (0.1°C units)
    uint8_t humidity;           // Humidity (%)
    bool locked;                // Lock state
    uint8_t position;           // Position (0-100%)
    uint8_t battery;            // Battery level (%)
    
    // DP Mappings (device-specific)
    tuya_dp_mapping_t dp_map[TUYA_DEVICE_MAX_DP];
    uint8_t dp_count;
    
    bool enabled;               // Device enabled flag
    uint32_t last_seen;         // Last update timestamp (seconds)
} tuya_device_t;

/**
 * @brief Initialize device registry
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_init(void);

/**
 * @brief Get all devices
 * @param devices Output array
 * @param max Maximum number of devices
 * @param count Pointer to store actual count
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_get_all(tuya_device_t *devices, int max, int *count);

/**
 * @brief Get device by ID
 * @param device_id Device ID
 * @param device Output device structure
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t tuya_devices_get_by_id(const char *device_id, tuya_device_t *device);

/**
 * @brief Add or update device
 * @param device Device structure
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_add_or_update(const tuya_device_t *device);

/**
 * @brief Remove device
 * @param device_id Device ID
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_remove(const char *device_id);

/**
 * @brief Update device state
 * @param device_id Device ID
 * @param state New state
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_set_state(const char *device_id, tuya_device_state_t state);

/**
 * @brief Update device switch value
 * @param device_id Device ID
 * @param on Switch state
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_set_switch(const char *device_id, bool on);

/**
 * @brief Update device brightness
 * @param device_id Device ID
 * @param brightness Brightness (0-100)
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_set_brightness(const char *device_id, uint8_t brightness);

/**
 * @brief Update device contact sensor
 * @param device_id Device ID
 * @param open Contact state (true=open)
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_set_contact(const char *device_id, bool open);

/**
 * @brief Update device motion sensor
 * @param device_id Device ID
 * @param motion Motion detected
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_set_motion(const char *device_id, bool motion);

/**
 * @brief Map device to virtual zone
 * @param device_id Device ID
 * @param zone_id Virtual zone ID (67-96)
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_map_to_zone(const char *device_id, int zone_id);

/**
 * @brief Map device to virtual relay
 * @param device_id Device ID
 * @param relay_id Virtual relay ID (32-63)
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_map_to_relay(const char *device_id, int relay_id);

/**
 * @brief Get device name for type
 * @param type Device type
 * @return Device type name string
 */
const char* tuya_device_type_name(tuya_device_type_t type);

/**
 * @brief Load devices from storage
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_load(void);

/**
 * @brief Save devices to storage
 * @return ESP_OK on success
 */
esp_err_t tuya_devices_save(void);

#ifdef __cplusplus
}
#endif

#endif // TUYA_DEVICES_H
