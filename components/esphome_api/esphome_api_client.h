/**
 * @file esphome_api_client.h
 * @brief ESPHome Native API client for Sentinel-ESP
 * 
 * Enables Sentinel alarm system to control ESPHome devices using their native API:
 * - Binary sensors (motion, door/window contacts) as virtual zones
 * - Switches/lights as virtual relays
 * - Real-time state updates via subscriptions
 * - Direct TCP communication (port 6053)
 * 
 * Compatible with ALL ESPHome devices (ESP8266, ESP32 variants)
 * 
 * @author Sentinel-ESP Team
 * @version 1.0.0
 * @date 2026-01-29
 */

#ifndef ESPHOME_API_CLIENT_H
#define ESPHOME_API_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../storage/storage_mgr.h"  // For esphome_device_t typedef

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ESPHOME_DEVICES 32
#define ESPHOME_API_PORT 6053

/** ESPHome device types */
typedef enum {
    ESPHOME_ENTITY_UNKNOWN = 0,
    ESPHOME_ENTITY_BINARY_SENSOR = 1,    // Motion, contact, etc.
    ESPHOME_ENTITY_SENSOR = 2,            // Temperature, humidity
    ESPHOME_ENTITY_SWITCH = 3,            // Relay, switch
    ESPHOME_ENTITY_LIGHT = 4,             // Light (on/off or dimmable)
    ESPHOME_ENTITY_FAN = 5,               // Fan control
    ESPHOME_ENTITY_COVER = 6,             // Blinds, garage door
    ESPHOME_ENTITY_CLIMATE = 7,           // Thermostat
} esphome_entity_type_t;

// Note: esphome_device_t is defined in storage_mgr.h

/** ESPHome entity (sensor, switch, etc.) */
typedef struct {
    uint32_t entity_key;                  // Unique entity key from ESPHome
    char object_id[64];                   // Entity object ID
    char name[64];                        // Entity name
    esphome_entity_type_t type;           // Entity type
    char device_hostname[64];             // Parent device
    bool current_state;                   // Current state (for binary)
    float current_value;                  // Current value (for numeric)
    int8_t virtual_zone_id;               // Mapped zone (-1 if not mapped)
    int8_t virtual_relay_id;              // Mapped relay (-1 if not mapped)
} esphome_entity_t;

/** ESPHome API state callback */
typedef void (*esphome_state_callback_t)(const char *hostname, uint32_t entity_key, bool state);

/**
 * @brief Initialize ESPHome API client
 * 
 * Sets up client infrastructure and prepares for device connections.
 * 
 * @return 0 on success, error code otherwise
 */
int esphome_api_init(void);

/**
 * @brief Connect to ESPHome device
 * 
 * Establishes TCP connection to ESPHome device's native API.
 * 
 * @param hostname Device hostname or IP address (e.g., "192.168.1.50")
 * @param port API port (default: 6053)
 * @param password API password (can be NULL if no auth)
 * @param encryption_key API encryption key (NULL for no encryption)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * esphome_api_connect("192.168.1.50", 6053, "mypassword", NULL);
 * ```
 */
int esphome_api_connect(const char *hostname, uint16_t port, 
                        const char *password, const char *encryption_key);

/**
 * @brief Disconnect from ESPHome device
 * 
 * @param hostname Device hostname
 * @return 0 on success, error code otherwise
 */
int esphome_api_disconnect(const char *hostname);

/**
 * @brief Get list of connected devices
 * 
 * @param devices Output buffer for device list
 * @param max_devices Size of devices buffer
 * @param count Output: number of devices returned
 * @return 0 on success, error code otherwise
 */
int esphome_api_get_devices(esphome_device_t *devices, int max_devices, int *count);

/**
 * @brief Get list of entities from device
 * 
 * Retrieves all sensors, switches, lights, etc. from connected device.
 * 
 * @param hostname Device hostname
 * @param entities Output buffer for entity list
 * @param max_entities Size of entities buffer
 * @param count Output: number of entities returned
 * @return 0 on success, error code otherwise
 */
int esphome_api_get_entities(const char *hostname, esphome_entity_t *entities, 
                              int max_entities, int *count);

/**
 * @brief Map ESPHome binary sensor to virtual zone
 * 
 * Links binary sensor (motion, contact) to Sentinel zone.
 * 
 * @param hostname Device hostname
 * @param entity_key Entity unique key
 * @param zone_id Virtual zone number (33-96)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Map "Garage Motion" sensor to zone 33
 * esphome_api_map_sensor_to_zone("garage-motion.local", 123456, 33);
 * ```
 */
int esphome_api_map_sensor_to_zone(const char *hostname, uint32_t entity_key, int zone_id);

/**
 * @brief Map ESPHome switch/light to virtual relay
 * 
 * Links switch or light to Sentinel relay output.
 * 
 * @param hostname Device hostname
 * @param entity_key Entity unique key
 * @param relay_id Virtual relay number (8-31)
 * @return 0 on success, error code otherwise
 */
int esphome_api_map_switch_to_relay(const char *hostname, uint32_t entity_key, int relay_id);

/**
 * @brief Control ESPHome switch/light
 * 
 * Sends on/off command to ESPHome entity.
 * 
 * @param hostname Device hostname
 * @param entity_key Entity unique key
 * @param state true = on, false = off
 * @return 0 on success, error code otherwise
 */
int esphome_api_set_switch(const char *hostname, uint32_t entity_key, bool state);

/**
 * @brief Find entity by virtual relay ID
 * 
 * @param relay_id Virtual relay ID (8-31)
 * @param hostname_out Output buffer for device hostname
 * @param hostname_len Size of hostname buffer
 * @param entity_key_out Output for entity key
 * @return 0 on success, -1 if not found
 */
int esphome_api_find_entity_by_relay(int relay_id, char *hostname_out, size_t hostname_len, uint32_t *entity_key_out);

/**
 * @brief Discover entities from all connected ESPHome devices
 * 
 * Connects to enabled devices and discovers their entities (switches, lights, etc.)
 * for mapping to virtual relays and zones.
 * 
 * @return 0 on success, error code otherwise
 */
int esphome_api_discover_all_entities(void);

/**
 * @brief Set brightness of ESPHome light
 * 
 * @param hostname Device hostname
 * @param entity_key Entity unique key
 * @param brightness Brightness (0.0 - 1.0)
 * @return 0 on success, error code otherwise
 */
int esphome_api_set_brightness(const char *hostname, uint32_t entity_key, float brightness);

/**
 * @brief Subscribe to entity state changes
 * 
 * Registers callback for real-time entity updates.
 * 
 * @param hostname Device hostname
 * @param entity_key Entity unique key
 * @param callback Function to call on state change
 * @return 0 on success, error code otherwise
 */
int esphome_api_subscribe_state(const char *hostname, uint32_t entity_key, 
                                 esphome_state_callback_t callback);

/**
 * @brief Start ESPHome API client task
 * 
 * Launches background task for connection management and message processing.
 * 
 * @return 0 on success, error code otherwise
 */
int esphome_api_start_task(void);

/**
 * @brief Save ESPHome configuration to NVS
 * 
 * @return 0 on success, error code otherwise
 */
int esphome_api_save_config(void);

/**
 * @brief Load ESPHome configuration from NVS
 * 
 * @return 0 on success, error code otherwise
 */
int esphome_api_load_config(void);

/**
 * @brief Discover ESPHome devices on network
 * 
 * Uses mDNS to find ESPHome devices advertising _esphomelib._tcp service.
 * 
 * @param found_callback Called for each discovered device
 * @return Number of devices found
 */
int esphome_api_discover_devices(void (*found_callback)(const char *hostname, const char *ip));

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_API_CLIENT_H
