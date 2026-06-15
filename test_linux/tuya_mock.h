/**
 * @file tuya_mock.h
 * @brief Mock Tuya manager header for Linux testing
 */

#ifndef TUYA_MOCK_H
#define TUYA_MOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef int esp_err_t;
#define ESP_OK 0

#define TUYA_DEVICE_MAX_COUNT 32
#define TUYA_DEVICE_ID_MAX_LEN 32
#define TUYA_DEVICE_NAME_MAX_LEN 64

typedef enum {
    TUYA_DEV_TYPE_SWITCH = 1,
    TUYA_DEV_TYPE_LIGHT = 2,
    TUYA_DEV_TYPE_DOOR_SENSOR = 4,
    TUYA_DEV_TYPE_MOTION_SENSOR = 5,
} tuya_device_type_t;

typedef enum {
    TUYA_DEV_STATE_OFFLINE = 0,
    TUYA_DEV_STATE_ONLINE = 1,
} tuya_device_state_t;

typedef enum {
    TUYA_CAP_SWITCH = (1 << 0),
    TUYA_CAP_BRIGHTNESS = (1 << 1),
    TUYA_CAP_CONTACT = (1 << 4),
    TUYA_CAP_MOTION = (1 << 5),
    TUYA_CAP_BATTERY = (1 << 10),
} tuya_capability_t;

typedef struct {
    char device_id[TUYA_DEVICE_ID_MAX_LEN];
    char name[TUYA_DEVICE_NAME_MAX_LEN];
    char description[128];
    char location[128];
    tuya_device_type_t type;
    tuya_device_state_t state;
    uint32_t capabilities;
    int virtual_zone_id;
    int virtual_relay_id;
    bool switch_on;
    uint8_t brightness;
    bool contact_open;
    bool motion_detected;
    uint8_t battery;
    bool enabled;
} tuya_device_t;

esp_err_t tuya_mgr_init(void *callback, void *user_data);
void tuya_mgr_deinit(void);
bool tuya_mgr_is_connected(void);
esp_err_t tuya_mgr_discover_devices(uint32_t timeout_ms);
esp_err_t tuya_mgr_enable_pairing(uint16_t duration_sec);
esp_err_t tuya_mgr_get_devices(tuya_device_t *devices, int max, int *count);
esp_err_t tuya_mgr_get_device(const char *device_id, tuya_device_t *device);
esp_err_t tuya_mgr_remove_device(const char *device_id);
esp_err_t tuya_mgr_set_switch(const char *device_id, bool on);
esp_err_t tuya_mgr_set_brightness(const char *device_id, uint8_t brightness);
esp_err_t tuya_mgr_set_color(const char *device_id, uint32_t rgb);
esp_err_t tuya_mgr_set_color_temp(const char *device_id, uint16_t mireds);
esp_err_t tuya_mgr_set_lock(const char *device_id, bool lock);
esp_err_t tuya_mgr_map_to_zone(const char *device_id, int zone_id);
esp_err_t tuya_mgr_map_to_relay(const char *device_id, int relay_id);
esp_err_t tuya_mgr_get_zone_state(int zone_id, bool *triggered);
esp_err_t tuya_mgr_set_relay_state(int relay_id, bool on);
esp_err_t tuya_mgr_save_config(void);
esp_err_t tuya_mgr_load_config(void);
esp_err_t tuya_mgr_reset_wifi(void);
const char* tuya_device_type_name(tuya_device_type_t type);

#endif // TUYA_MOCK_H
