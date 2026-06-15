/**
 * @file tuya_mock.c
 * @brief Mock implementation of Tuya manager for Linux testing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "cjson/cJSON.h"

// Mock ESP types
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NOT_FOUND -4
#define ESP_ERR_NOT_SUPPORTED -5

// Mock device limits
#define TUYA_DEVICE_MAX_COUNT 32
#define TUYA_DEVICE_ID_MAX_LEN 32
#define TUYA_DEVICE_NAME_MAX_LEN 64

// Device types
typedef enum {
    TUYA_DEV_TYPE_SWITCH = 1,
    TUYA_DEV_TYPE_LIGHT = 2,
    TUYA_DEV_TYPE_DOOR_SENSOR = 4,
    TUYA_DEV_TYPE_MOTION_SENSOR = 5,
} tuya_device_type_t;

// Device state
typedef enum {
    TUYA_DEV_STATE_OFFLINE = 0,
    TUYA_DEV_STATE_ONLINE = 1,
} tuya_device_state_t;

// Capabilities
typedef enum {
    TUYA_CAP_SWITCH = (1 << 0),
    TUYA_CAP_BRIGHTNESS = (1 << 1),
    TUYA_CAP_CONTACT = (1 << 4),
    TUYA_CAP_MOTION = (1 << 5),
    TUYA_CAP_BATTERY = (1 << 10),
} tuya_capability_t;

// Mock device structure
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

// Mock registry
static tuya_device_t mock_devices[TUYA_DEVICE_MAX_COUNT];
static int mock_device_count = 0;
static bool mock_connected = true;

// Initialize with mock devices
esp_err_t tuya_mgr_init(void *callback, void *user_data) {
    (void)callback;
    (void)user_data;
    
    printf("TUYA_MOCK: Initializing Tuya manager (mock)\n");
    
    // Load devices from tuya.json
    mock_device_count = 0;
    FILE *f = fopen("data/tuya.json", "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *data = malloc(len + 1);
        fread(data, 1, len, f);
        data[len] = '\0';
        fclose(f);
        
        cJSON *root = cJSON_Parse(data);
        if (root && cJSON_IsArray(root)) {
            int count = cJSON_GetArraySize(root);
            for (int i = 0; i < count && mock_device_count < TUYA_DEVICE_MAX_COUNT; i++) {
                cJSON *item = cJSON_GetArrayItem(root, i);
                if (item) {
                    tuya_device_t *dev = &mock_devices[mock_device_count];
                    memset(dev, 0, sizeof(tuya_device_t));
                    
                    // Basic fields
                    cJSON *j_device_id = cJSON_GetObjectItem(item, "device_id");
                    if (j_device_id) strcpy(dev->device_id, j_device_id->valuestring);
                    else sprintf(dev->device_id, "mock_%d", i);
                    
                    cJSON *j_name = cJSON_GetObjectItem(item, "name");
                    if (j_name) strcpy(dev->name, j_name->valuestring);
                    
                    cJSON *j_enabled = cJSON_GetObjectItem(item, "enabled");
                    dev->enabled = cJSON_IsTrue(j_enabled);
                    
                    // Zone
                    cJSON *j_zone = cJSON_GetObjectItem(item, "zone");
                    if (j_zone) {
                        cJSON *z_id = cJSON_GetObjectItem(j_zone, "id");
                        dev->virtual_zone_id = z_id ? z_id->valueint : 0;
                        cJSON *j_desc = cJSON_GetObjectItem(j_zone, "description");
                        if (j_desc) strcpy(dev->description, j_desc->valuestring);
                        cJSON *j_loc = cJSON_GetObjectItem(j_zone, "location");
                        if (j_loc) strcpy(dev->location, j_loc->valuestring);
                        dev->type = TUYA_DEV_TYPE_MOTION_SENSOR; // Assume motion for zones
                        dev->capabilities = TUYA_CAP_MOTION;
                        dev->motion_detected = false;
                    }
                    
                    // Relay
                    cJSON *j_relay = cJSON_GetObjectItem(item, "relay");
                    if (j_relay) {
                        cJSON *r_id = cJSON_GetObjectItem(j_relay, "id");
                        dev->virtual_relay_id = r_id ? r_id->valueint : 0;
                        cJSON *j_desc = cJSON_GetObjectItem(j_relay, "description");
                        if (j_desc) strcpy(dev->description, j_desc->valuestring);
                        cJSON *j_loc = cJSON_GetObjectItem(j_relay, "location");
                        if (j_loc) strcpy(dev->location, j_loc->valuestring);
                        dev->type = TUYA_DEV_TYPE_SWITCH; // Assume switch for relays
                        dev->capabilities = TUYA_CAP_SWITCH;
                        dev->switch_on = false;
                    }
                    
                    dev->state = TUYA_DEV_STATE_ONLINE;
                    dev->battery = 100;
                    
                    if (dev->enabled) {
                        mock_device_count++;
                    }
                }
            }
        }
        cJSON_Delete(root);
        free(data);
    }
    
    mock_connected = true;
    
    printf("TUYA_MOCK: Initialized with %d mock devices\n", mock_device_count);
    return ESP_OK;
}

void tuya_mgr_deinit(void) {
    printf("TUYA_MOCK: Deinitializing\n");
    mock_device_count = 0;
    mock_connected = false;
}

bool tuya_mgr_is_connected(void) {
    return mock_connected;
}

esp_err_t tuya_mgr_discover_devices(uint32_t timeout_ms) {
    (void)timeout_ms;
    printf("TUYA_MOCK: Discovery started (mock)\n");
    return ESP_OK;
}

esp_err_t tuya_mgr_enable_pairing(uint16_t duration_sec) {
    printf("TUYA_MOCK: Pairing enabled for %d seconds (mock)\n", duration_sec);
    return ESP_OK;
}

esp_err_t tuya_mgr_get_devices(tuya_device_t *devices, int max, int *count) {
    if (!devices || !count) return ESP_ERR_INVALID_ARG;
    
    int copy_count = (mock_device_count < max) ? mock_device_count : max;
    memcpy(devices, mock_devices, copy_count * sizeof(tuya_device_t));
    *count = copy_count;
    return ESP_OK;
}

esp_err_t tuya_mgr_get_device(const char *device_id, tuya_device_t *device) {
    if (!device_id || !device) return ESP_ERR_INVALID_ARG;
    
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            memcpy(device, &mock_devices[i], sizeof(tuya_device_t));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_remove_device(const char *device_id) {
    if (!device_id) return ESP_ERR_INVALID_ARG;
    
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            if (i < mock_device_count - 1) {
                memmove(&mock_devices[i], &mock_devices[i + 1],
                       (mock_device_count - i - 1) * sizeof(tuya_device_t));
            }
            mock_device_count--;
            printf("TUYA_MOCK: Removed device %s\n", device_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_set_switch(const char *device_id, bool on) {
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            mock_devices[i].switch_on = on;
            printf("TUYA_MOCK: Device %s switch: %s\n", device_id, on ? "ON" : "OFF");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_set_brightness(const char *device_id, uint8_t brightness) {
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            mock_devices[i].brightness = brightness;
            printf("TUYA_MOCK: Device %s brightness: %d%%\n", device_id, brightness);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_set_color(const char *device_id, uint32_t rgb) {
    (void)device_id;
    (void)rgb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_set_color_temp(const char *device_id, uint16_t mireds) {
    (void)device_id;
    (void)mireds;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_set_lock(const char *device_id, bool lock) {
    (void)device_id;
    (void)lock;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_map_to_zone(const char *device_id, int zone_id) {
    if (zone_id != -1 && (zone_id < 67 || zone_id > 96)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            mock_devices[i].virtual_zone_id = zone_id;
            printf("TUYA_MOCK: Device %s mapped to zone %d\n", device_id, zone_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_map_to_relay(const char *device_id, int relay_id) {
    if (relay_id != -1 && (relay_id < 32 || relay_id > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < mock_device_count; i++) {
        if (strcmp(mock_devices[i].device_id, device_id) == 0) {
            mock_devices[i].virtual_relay_id = relay_id;
            printf("TUYA_MOCK: Device %s mapped to relay %d\n", device_id, relay_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_get_zone_state(int zone_id, bool *triggered) {
    if (!triggered || zone_id < 67 || zone_id > 96) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].virtual_zone_id == zone_id) {
            if (mock_devices[i].type == TUYA_DEV_TYPE_DOOR_SENSOR) {
                *triggered = mock_devices[i].contact_open;
                return ESP_OK;
            } else if (mock_devices[i].type == TUYA_DEV_TYPE_MOTION_SENSOR) {
                *triggered = mock_devices[i].motion_detected;
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_set_relay_state(int relay_id, bool on) {
    if (relay_id < 32 || relay_id > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].virtual_relay_id == relay_id) {
            return tuya_mgr_set_switch(mock_devices[i].device_id, on);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_save_config(void) {
    printf("TUYA_MOCK: Config saved (mock)\n");
    return ESP_OK;
}

esp_err_t tuya_mgr_load_config(void) {
    printf("TUYA_MOCK: Config loaded (mock)\n");
    return ESP_OK;
}

esp_err_t tuya_mgr_reset_wifi(void) {
    printf("TUYA_MOCK: WiFi reset (mock)\n");
    return ESP_OK;
}

const char* tuya_device_type_name(tuya_device_type_t type) {
    switch (type) {
        case TUYA_DEV_TYPE_SWITCH: return "Switch";
        case TUYA_DEV_TYPE_LIGHT: return "Light";
        case TUYA_DEV_TYPE_DOOR_SENSOR: return "Door Sensor";
        case TUYA_DEV_TYPE_MOTION_SENSOR: return "Motion Sensor";
        default: return "Unknown";
    }
}
