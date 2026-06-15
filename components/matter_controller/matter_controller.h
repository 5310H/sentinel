/**
 * @file matter_controller.h
 * @brief Matter protocol controller for Sentinel-ESP
 * 
 * Enables Sentinel alarm system to control Matter devices:
 * - Motion sensors as virtual zones (wireless zone expansion)
 * - Smart switches/lights for alarm responses
 * - Door locks for security automation
 * - Environmental sensors (temperature, leak, smoke)
 * 
 * Supports both ESPHome Matter devices and commercial Matter products
 * (Philips Hue, Aqara, Yale locks, etc.)
 * 
 * @author Sentinel-ESP Team
 * @version 1.0.0
 * @date 2026-01-29
 */

#ifndef MATTER_CONTROLLER_H
#define MATTER_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of Matter devices that can be managed */
#define MAX_MATTER_DEVICES 64

/** Matter device types */
typedef enum {
    MATTER_DEVICE_UNKNOWN = 0,
    MATTER_DEVICE_ON_OFF_SWITCH = 1,        // Smart switch/relay
    MATTER_DEVICE_DIMMABLE_LIGHT = 2,       // Dimmable bulb
    MATTER_DEVICE_COLOR_LIGHT = 3,          // RGB color bulb
    MATTER_DEVICE_OCCUPANCY_SENSOR = 4,     // Motion sensor
    MATTER_DEVICE_CONTACT_SENSOR = 5,       // Door/window sensor
    MATTER_DEVICE_DOOR_LOCK = 6,            // Smart lock
    MATTER_DEVICE_TEMPERATURE_SENSOR = 7,   // Temperature sensor
    MATTER_DEVICE_HUMIDITY_SENSOR = 8,      // Humidity sensor
    MATTER_DEVICE_SMOKE_DETECTOR = 9,       // Smoke/CO alarm
    MATTER_DEVICE_WATER_LEAK_SENSOR = 10,   // Water leak detector
} matter_device_type_t;

/** Matter device information */
typedef struct {
    uint64_t node_id;                       // Unique Matter node ID
    uint16_t endpoint_id;                   // Device endpoint
    matter_device_type_t device_type;       // Device category
    char name[64];                          // User-friendly name
    bool is_commissioned;                   // Pairing status
    bool is_online;                         // Current connectivity
    int8_t virtual_zone_id;                 // Mapped zone (-1 if not a sensor)
    int8_t virtual_relay_id;                // Mapped relay (-1 if not output)
    uint32_t last_update_ms;                // Last state update timestamp
} matter_device_t;

/** Matter sensor state callback */
typedef void (*matter_sensor_callback_t)(uint64_t node_id, uint16_t endpoint, bool state);

/**
 * @brief Initialize Matter controller
 * 
 * Sets up Matter stack and prepares for device commissioning.
 * Must be called before any other Matter functions.
 * 
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * if (matter_controller_init() == 0) {
 *     ESP_LOGI(TAG, "Matter controller ready");
 * }
 * ```
 */
int matter_controller_init(void);

/**
 * @brief Commission a new Matter device
 * 
 * Pairs a Matter device using QR code or manual pairing code.
 * Device must be in pairing mode (factory reset or setup mode).
 * 
 * Process:
 * 1. User scans QR code or enters pairing code
 * 2. Sentinel discovers device over network
 * 3. Secure commissioning establishes crypto keys
 * 4. Device joins Matter fabric
 * 
 * @param pairing_code Setup code from device (format: "MT:Y.K9QAO00KNY0648G00")
 * @param device_name User-friendly name for device
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Commission ESPHome motion sensor
 * matter_commission_device("MT:Y.K9QAO00KNY0648G00", "Garage Motion");
 * ```
 */
int matter_commission_device(const char *pairing_code, const char *device_name);

/**
 * @brief Remove commissioned Matter device
 * 
 * Unpairs device and removes from Matter fabric.
 * Device will need re-commissioning to reconnect.
 * 
 * @param node_id Matter node ID to remove
 * @return 0 on success, error code otherwise
 */
int matter_remove_device(uint64_t node_id);

/**
 * @brief Get list of all commissioned devices
 * 
 * Retrieves array of all Matter devices in the fabric.
 * 
 * @param devices Output buffer for device list
 * @param max_devices Size of devices buffer
 * @param count Output: number of devices returned
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * matter_device_t devices[MAX_MATTER_DEVICES];
 * int count;
 * matter_get_devices(devices, MAX_MATTER_DEVICES, &count);
 * for (int i = 0; i < count; i++) {
 *     printf("Device: %s (Node ID: %llu)\n", devices[i].name, devices[i].node_id);
 * }
 * ```
 */
int matter_get_devices(matter_device_t *devices, int max_devices, int *count);

/**
 * @brief Map Matter sensor to virtual zone
 * 
 * Links Matter occupancy/contact sensor to Sentinel zone.
 * When sensor triggers, corresponding zone activates.
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param zone_id Virtual zone number (33-96)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Map motion sensor to zone 33
 * matter_map_sensor_to_zone(0x1234567890ABCDEF, 1, 33);
 * ```
 */
int matter_map_sensor_to_zone(uint64_t node_id, uint16_t endpoint_id, int zone_id);

/**
 * @brief Map Matter switch to virtual relay
 * 
 * Links Matter on/off device to Sentinel relay output.
 * Commands to relay will control Matter device.
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param relay_id Virtual relay number (8-15)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Map smart switch to virtual relay 8
 * matter_map_switch_to_relay(0xABCDEF1234567890, 1, 8);
 * ```
 */
int matter_map_switch_to_relay(uint64_t node_id, uint16_t endpoint_id, int relay_id);

/**
 * @brief Control Matter on/off device
 * 
 * Sends on or off command to Matter switch/light/lock.
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param on true = turn on, false = turn off
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Turn on Philips Hue bulb
 * matter_send_on_off(hue_node_id, 1, true);
 * ```
 */
int matter_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on);

/**
 * @brief Set brightness of Matter dimmable light
 * 
 * Controls light brightness level (0-254).
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param level Brightness (0=off, 254=maximum)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Set to 50% brightness
 * matter_send_brightness(node_id, 1, 127);
 * ```
 */
int matter_send_brightness(uint64_t node_id, uint16_t endpoint_id, uint8_t level);

/**
 * @brief Lock/unlock Matter door lock
 * 
 * Controls smart lock state.
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param lock true = lock, false = unlock
 * @param pin_code Optional PIN code for unlock (NULL if not required)
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * // Lock door when alarm arms
 * matter_send_lock_unlock(lock_node_id, 1, true, NULL);
 * ```
 */
int matter_send_lock_unlock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code);

/**
 * @brief Read current state of Matter sensor
 * 
 * Queries device for current sensor value.
 * For binary sensors (motion, contact), returns true/false.
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param state Output: current sensor state
 * @return 0 on success, error code otherwise
 */
int matter_read_sensor_state(uint64_t node_id, uint16_t endpoint_id, bool *state);

/**
 * @brief Subscribe to Matter sensor state changes
 * 
 * Registers callback for real-time sensor updates.
 * Callback fires when sensor state changes (motion detected, door opens, etc.)
 * 
 * @param node_id Matter device node ID
 * @param endpoint_id Device endpoint (usually 1)
 * @param callback Function to call on state change
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * void motion_detected(uint64_t node_id, uint16_t endpoint, bool occupied) {
 *     if (occupied && engine_is_armed()) {
 *         trigger_alarm(33, "Motion in garage");
 *     }
 * }
 * 
 * matter_subscribe_sensor(garage_motion_id, 1, motion_detected);
 * ```
 */
int matter_subscribe_sensor(uint64_t node_id, uint16_t endpoint_id, matter_sensor_callback_t callback);

/**
 * @brief Start Matter controller task
 * 
 * Launches background task for Matter event processing.
 * Handles incoming attribute reports, device online/offline events, etc.
 * 
 * @return 0 on success, error code otherwise
 */
int matter_controller_start_task(void);

/**
 * @brief Save Matter device configuration to NVS
 * 
 * Persists device mappings (zones/relays) and names.
 * Matter fabric credentials are automatically saved by Matter stack.
 * 
 * @return 0 on success, error code otherwise
 */
int matter_save_config(void);

/**
 * @brief Load Matter device configuration from NVS
 * 
 * Restores device mappings and names after reboot.
 * 
 * @return 0 on success, error code otherwise
 */
int matter_load_config(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_CONTROLLER_H
