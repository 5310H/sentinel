/**
 * @file zigbee_mock.h
 * @brief Mock Zigbee implementation for Linux testing
 * 
 * Uses USB serial port for connecting to real Zigbee coordinators:
 * - /dev/ttyUSB0 - Most USB-to-serial adapters (FTDI, CH340)
 * - /dev/ttyACM0 - CDC ACM devices (some coordinators)
 * - /dev/cu.usbserial* - macOS
 * 
 * Supported coordinators:
 * - TI CC2652P USB stick
 * - ConBee II USB stick
 * - Sonoff Zigbee Bridge (flashed with coordinator firmware)
 */

#ifndef ZIGBEE_MOCK_H
#define ZIGBEE_MOCK_H

#include <stdint.h>
#include <stdbool.h>

// Mock ESP types
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NOT_FOUND -4
#define ESP_ERR_NO_MEM -5
#define ESP_ERR_NOT_SUPPORTED -6

// Device capabilities (bitmask)
#define ZIGBEE_CAP_ON_OFF           (1 << 0)   // 0x001
#define ZIGBEE_CAP_LEVEL            (1 << 1)   // 0x002
#define ZIGBEE_CAP_COLOR            (1 << 2)   // 0x004
#define ZIGBEE_CAP_TEMPERATURE      (1 << 3)   // 0x008
#define ZIGBEE_CAP_HUMIDITY         (1 << 4)   // 0x010
#define ZIGBEE_CAP_BATTERY          (1 << 5)   // 0x020
#define ZIGBEE_CAP_CONTACT          (1 << 6)   // 0x040
#define ZIGBEE_CAP_OCCUPANCY        (1 << 7)   // 0x080
#define ZIGBEE_CAP_LEAK             (1 << 8)   // 0x100
#define ZIGBEE_CAP_SMOKE            (1 << 9)   // 0x200
#define ZIGBEE_CAP_LOCK             (1 << 10)  // 0x400

// Device structure (matching zigbee_devices.h)
#define ZIGBEE_MAX_DEVICES 64
#define ZIGBEE_MAX_NAME_LEN 32

typedef enum {
    ZIGBEE_DEVICE_UNKNOWN = 0,
    ZIGBEE_DEVICE_SWITCH,
    ZIGBEE_DEVICE_LIGHT,
    ZIGBEE_DEVICE_COLOR_LIGHT,
    ZIGBEE_DEVICE_OUTLET,
    ZIGBEE_DEVICE_DOOR_SENSOR,
    ZIGBEE_DEVICE_MOTION_SENSOR,
    ZIGBEE_DEVICE_TEMPERATURE,
    ZIGBEE_DEVICE_HUMIDITY,
    ZIGBEE_DEVICE_LEAK_SENSOR,
    ZIGBEE_DEVICE_SMOKE_DETECTOR,
    ZIGBEE_DEVICE_LOCK,
    ZIGBEE_DEVICE_THERMOSTAT,
    ZIGBEE_DEVICE_BUTTON,
    ZIGBEE_DEVICE_DIMMER,
    ZIGBEE_DEVICE_BLIND
} zigbee_device_type_t;

typedef enum {
    ZIGBEE_STATE_OFFLINE = 0,
    ZIGBEE_STATE_ONLINE,
    ZIGBEE_STATE_JOINING,
    ZIGBEE_STATE_LEAVING
} zigbee_device_state_t;

typedef struct {
    uint16_t short_addr;
    char name[ZIGBEE_MAX_NAME_LEN];
    zigbee_device_type_t type;
    zigbee_device_state_t state;
    uint16_t capabilities;
    uint8_t endpoint;
    
    int zone_id;
    int relay_id;
    
    bool current_on_off;
    uint8_t current_level;
    uint32_t current_color_rgb;
    float current_temperature;
    float current_humidity;
    uint8_t current_battery;
    bool current_contact;
    bool current_occupancy;
} zigbee_device_t;

// Network statistics
typedef struct {
    int total_devices;
    int online_devices;
    int offline_devices;
    uint32_t commands_sent;
    uint32_t commands_failed;
    uint32_t frames_received;
    int low_battery_devices;
    uint32_t uptime_seconds;
} zigbee_network_stats_t;

// Manager functions
esp_err_t zigbee_mgr_init(const void *config, void *callback);
void zigbee_mgr_deinit(void);
bool zigbee_mgr_is_connected(void);
esp_err_t zigbee_mgr_enable_pairing(uint16_t duration_sec);
esp_err_t zigbee_mgr_discover_devices(uint32_t timeout_ms);
esp_err_t zigbee_mgr_get_devices(zigbee_device_t *devices, int *count);
const zigbee_device_t* zigbee_mgr_get_device(uint16_t short_addr);
esp_err_t zigbee_mgr_update_device(uint16_t short_addr, const char *name,
                                    int zone_id, int relay_id, int enabled);
esp_err_t zigbee_mgr_remove_device(uint16_t short_addr);
esp_err_t zigbee_mgr_set_on_off(uint16_t short_addr, bool on);
esp_err_t zigbee_mgr_set_level(uint16_t short_addr, uint8_t level);
esp_err_t zigbee_mgr_set_color(uint16_t short_addr, uint32_t rgb);
esp_err_t zigbee_mgr_set_color_temp(uint16_t short_addr, uint16_t mireds);
esp_err_t zigbee_mgr_set_lock(uint16_t short_addr, bool locked);
esp_err_t zigbee_mgr_set_thermostat(uint16_t short_addr, float temperature);
int zigbee_mgr_get_battery_level(uint16_t short_addr);
bool zigbee_mgr_is_battery_low(uint16_t short_addr);
esp_err_t zigbee_mgr_get_statistics(zigbee_network_stats_t *stats);
esp_err_t zigbee_mgr_map_to_zone(uint16_t short_addr, int zone_id);
esp_err_t zigbee_mgr_map_to_relay(uint16_t short_addr, int relay_id);
bool zigbee_mgr_get_zone_state(int zone_id);
esp_err_t zigbee_mgr_set_relay_state(int relay_id, bool on);

// Device type helper
const char* zigbee_device_type_name(zigbee_device_type_t type);

// Phase 3: Groups, Scenes, Automations (Mock stubs)
#define ZIGBEE_MAX_GROUPS 16
#define ZIGBEE_MAX_SCENES 32

typedef struct {
    uint8_t group_id;
    char name[32];
    uint16_t devices[32];
    int device_count;
    bool enabled;
} zigbee_group_t;

typedef struct {
    uint8_t scene_id;
    char name[32];
    int device_count;
    bool enabled;
} zigbee_scene_t;

// Group functions
esp_err_t zigbee_groups_init(void);
esp_err_t zigbee_groups_create(const char *name, uint8_t *group_id);
esp_err_t zigbee_groups_delete(uint8_t group_id);
esp_err_t zigbee_groups_get_all(zigbee_group_t *groups, int *count);
esp_err_t zigbee_groups_set_on_off(uint8_t group_id, bool on);
esp_err_t zigbee_groups_set_level(uint8_t group_id, uint8_t level);
esp_err_t zigbee_groups_set_color(uint8_t group_id, uint32_t rgb);

// Scene functions
esp_err_t zigbee_scenes_init(void);
esp_err_t zigbee_scenes_create(const char *name, uint8_t *scene_id);
esp_err_t zigbee_scenes_delete(uint8_t scene_id);
esp_err_t zigbee_scenes_get_all(zigbee_scene_t *scenes, int *count);
esp_err_t zigbee_scenes_recall(uint8_t scene_id);

#endif // ZIGBEE_MOCK_H
