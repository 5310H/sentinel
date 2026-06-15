/**
 * @file matter_controller.h
 * @brief Matter controller header (mock-compatible)
 */

#ifndef MATTER_CONTROLLER_H
#define MATTER_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MATTER_DEVICES 64

typedef enum {
    MATTER_DEVICE_UNKNOWN = 0,
    MATTER_DEVICE_ON_OFF_SWITCH = 1,
    MATTER_DEVICE_DIMMABLE_LIGHT = 2,
    MATTER_DEVICE_COLOR_LIGHT = 3,
    MATTER_DEVICE_OCCUPANCY_SENSOR = 4,
    MATTER_DEVICE_CONTACT_SENSOR = 5,
    MATTER_DEVICE_DOOR_LOCK = 6,
} matter_device_type_t;

typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    matter_device_type_t device_type;
    char name[64];
    bool is_commissioned;
    bool is_online;
    int8_t virtual_zone_id;
    int8_t virtual_relay_id;
    uint32_t last_update_ms;
} matter_device_t;

// Core API
int matter_controller_init(void);
int matter_zones_init(void);
int matter_relays_init(void);
int matter_controller_start_task(void);

// Device management
int matter_commission_device(const char *pairing_code, const char *device_name);
int matter_remove_device(uint64_t node_id);
int matter_get_devices(matter_device_t *devices, int max_devices, int *count);

// Zone/relay mapping
int matter_map_sensor_to_zone(uint64_t node_id, uint16_t endpoint_id, int zone_id);
int matter_map_switch_to_relay(uint64_t node_id, uint16_t endpoint_id, int relay_id);

// Device control
int matter_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on);
int matter_send_brightness(uint64_t node_id, uint16_t endpoint_id, uint8_t level);
int matter_send_lock_unlock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code);

// Storage
int matter_save_config(void);
int matter_load_config(void);

// Alarm integration
int matter_trigger_alarm_response(void);
int matter_clear_alarm_response(void);

// Mock testing helpers (only in Linux build)
#ifndef ESP_PLATFORM
void mock_matter_trigger_sensor(int zone_id);
void mock_matter_list_devices(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // MATTER_CONTROLLER_H
