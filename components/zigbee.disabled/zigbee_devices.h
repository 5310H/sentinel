/**
 * @file zigbee_devices.h
 * @brief Zigbee device registry and state management
 */

#ifndef ZIGBEE_DEVICES_H
#define ZIGBEE_DEVICES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIGBEE_MAX_DEVICES 64
#define ZIGBEE_MAX_NAME_LEN 32
#define ZIGBEE_IEEE_ADDR_LEN 8

// Device types (similar to Tuya but Zigbee-specific)
typedef enum {
    ZIGBEE_DEVICE_UNKNOWN = 0,
    ZIGBEE_DEVICE_SWITCH,           // On/Off switch
    ZIGBEE_DEVICE_LIGHT,            // Dimmable light
    ZIGBEE_DEVICE_COLOR_LIGHT,      // Color RGB light
    ZIGBEE_DEVICE_OUTLET,           // Smart outlet
    ZIGBEE_DEVICE_DOOR_SENSOR,      // Contact sensor
    ZIGBEE_DEVICE_MOTION_SENSOR,    // PIR sensor
    ZIGBEE_DEVICE_TEMPERATURE,      // Temperature sensor
    ZIGBEE_DEVICE_HUMIDITY,         // Humidity sensor
    ZIGBEE_DEVICE_LEAK_SENSOR,      // Water leak detector
    ZIGBEE_DEVICE_SMOKE_DETECTOR,   // Smoke/fire alarm
    ZIGBEE_DEVICE_LOCK,             // Door lock
    ZIGBEE_DEVICE_THERMOSTAT,       // Climate control
    ZIGBEE_DEVICE_BUTTON,           // Wireless button/switch
    ZIGBEE_DEVICE_DIMMER,           // Dimmer switch
    ZIGBEE_DEVICE_BLIND             // Window blind/shade
} zigbee_device_type_t;

// Device state
typedef enum {
    ZIGBEE_STATE_OFFLINE = 0,
    ZIGBEE_STATE_ONLINE,
    ZIGBEE_STATE_JOINING,
    ZIGBEE_STATE_LEAVING
} zigbee_device_state_t;

// Device capabilities (bitmask)
#define ZIGBEE_CAP_ON_OFF           (1 << 0)   // 0x001
#define ZIGBEE_CAP_LEVEL            (1 << 1)   // 0x002 - Brightness/dimming
#define ZIGBEE_CAP_COLOR            (1 << 2)   // 0x004 - RGB color
#define ZIGBEE_CAP_COLOR_TEMP       (1 << 3)   // 0x008 - Color temperature
#define ZIGBEE_CAP_POSITION         (1 << 4)   // 0x010 - Blind position
#define ZIGBEE_CAP_LOCK             (1 << 5)   // 0x020 - Lock/unlock
#define ZIGBEE_CAP_TEMPERATURE      (1 << 6)   // 0x040 - Temperature reading
#define ZIGBEE_CAP_HUMIDITY         (1 << 7)   // 0x080 - Humidity reading
#define ZIGBEE_CAP_BATTERY          (1 << 8)   // 0x100 - Battery level
#define ZIGBEE_CAP_TAMPER           (1 << 9)   // 0x200 - Tamper detection
#define ZIGBEE_CAP_OCCUPANCY        (1 << 10)  // 0x400 - Motion/occupancy
#define ZIGBEE_CAP_CONTACT          (1 << 11)  // 0x800 - Door/window contact

// Zigbee device structure
typedef struct {
    uint8_t ieee_addr[ZIGBEE_IEEE_ADDR_LEN];  // IEEE 64-bit address
    uint16_t short_addr;                       // Network short address
    char name[ZIGBEE_MAX_NAME_LEN];
    zigbee_device_type_t type;
    zigbee_device_state_t state;
    uint16_t capabilities;                     // Bitmask of capabilities
    uint8_t endpoint;                          // Primary endpoint
    
    // Zone/Relay mapping
    int zone_id;                               // Mapped zone (50-64), 0 if unmapped
    int relay_id;                              // Mapped relay (64-95), 0 if unmapped
    
    // Current values
    bool current_on_off;
    uint8_t current_level;                     // 0-254
    uint32_t current_color_rgb;                // 0xRRGGBB
    uint16_t current_color_temp;               // Mireds
    float current_temperature;                 // °C
    float current_humidity;                    // %RH
    uint8_t current_battery;                   // %
    bool current_contact;                      // false=closed, true=open
    bool current_occupancy;                    // false=clear, true=occupied
    bool current_tamper;
    
    // Metadata
    uint32_t last_seen;                        // Timestamp of last communication
    char manufacturer[32];
    char model[32];
} zigbee_device_t;

/**
 * @brief Initialize device registry
 */
void zigbee_devices_init(void);

/**
 * @brief Add or update device in registry
 * @param device Device to add/update
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_add_or_update(const zigbee_device_t *device);

/**
 * @brief Remove device from registry
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_remove(uint16_t short_addr);

/**
 * @brief Get device by short address
 * @param short_addr Device short address
 * @return Pointer to device or NULL if not found
 */
const zigbee_device_t* zigbee_devices_get(uint16_t short_addr);

/**
 * @brief Get device by IEEE address
 * @param ieee_addr IEEE 64-bit address
 * @return Pointer to device or NULL if not found
 */
const zigbee_device_t* zigbee_devices_get_by_ieee(const uint8_t *ieee_addr);

/**
 * @brief Get all devices
 * @param devices Output array (must be at least ZIGBEE_MAX_DEVICES)
 * @param count Output: number of devices
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_get_all(zigbee_device_t *devices, int *count);

/**
 * @brief Get device count
 * @return Number of registered devices
 */
int zigbee_devices_get_count(void);

/**
 * @brief Update device on/off state
 * @param short_addr Device short address
 * @param on_off true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_set_on_off(uint16_t short_addr, bool on_off);

/**
 * @brief Update device level (brightness)
 * @param short_addr Device short address
 * @param level Level 0-254
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_set_level(uint16_t short_addr, uint8_t level);

/**
 * @brief Update device color (RGB)
 * @param short_addr Device short address
 * @param rgb RGB color in 0xRRGGBB format
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_set_color(uint16_t short_addr, uint32_t rgb);

/**
 * @brief Update device color temperature
 * @param short_addr Device short address
 * @param mireds Color temperature in mireds (153-500)
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_set_color_temp(uint16_t short_addr, uint16_t mireds);

/**
 * @brief Map device to zone
 * @param short_addr Device short address
 * @param zone_id Zone ID (50-64), 0 to unmap
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_map_to_zone(uint16_t short_addr, int zone_id);

/**
 * @brief Map device to relay
 * @param short_addr Device short address
 * @param relay_id Relay ID (64-95), 0 to unmap
 * @return ESP_OK on success
 */
esp_err_t zigbee_devices_map_to_relay(uint16_t short_addr, int relay_id);

/**
 * @brief Get device type name as string
 * @param type Device type
 * @return Type name string
 */
const char* zigbee_device_type_name(zigbee_device_type_t type);

#ifdef __cplusplus
}
#endif

#endif // ZIGBEE_DEVICES_H
