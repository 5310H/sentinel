/**
 * @file tuya_devices.c
 * @brief Tuya Device Management Implementation
 */

#include "tuya_devices.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tuya_devs";

// Device registry
static tuya_device_t device_registry[TUYA_DEVICE_MAX_COUNT];
static int device_count = 0;

esp_err_t tuya_devices_init(void) {
    memset(device_registry, 0, sizeof(device_registry));
    device_count = 0;
    
    // Initialize all virtual zone/relay IDs to -1 (unmapped)
    for (int i = 0; i < TUYA_DEVICE_MAX_COUNT; i++) {
        device_registry[i].virtual_zone_id = -1;
        device_registry[i].virtual_relay_id = -1;
    }
    
    ESP_LOGI(TAG, "Device registry initialized");
    return ESP_OK;
}

esp_err_t tuya_devices_get_all(tuya_device_t *devices, int max, int *count) {
    if (!devices || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int copy_count = (device_count < max) ? device_count : max;
    memcpy(devices, device_registry, copy_count * sizeof(tuya_device_t));
    *count = copy_count;
    
    return ESP_OK;
}

esp_err_t tuya_devices_get_by_id(const char *device_id, tuya_device_t *device) {
    if (!device_id || !device) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            memcpy(device, &device_registry[i], sizeof(tuya_device_t));
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_add_or_update(const tuya_device_t *device) {
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if device already exists
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device->device_id) == 0) {
            // Update existing device
            memcpy(&device_registry[i], device, sizeof(tuya_device_t));
            ESP_LOGI(TAG, "Updated device: %s (%s)", device->name, device->device_id);
            return ESP_OK;
        }
    }
    
    // Add new device
    if (device_count >= TUYA_DEVICE_MAX_COUNT) {
        ESP_LOGE(TAG, "Device registry full (%d devices)", TUYA_DEVICE_MAX_COUNT);
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(&device_registry[device_count], device, sizeof(tuya_device_t));
    device_registry[device_count].last_seen = (uint32_t)time(NULL);
    device_count++;
    
    ESP_LOGI(TAG, "Added device: %s (%s)", device->name, device->device_id);
    return ESP_OK;
}

esp_err_t tuya_devices_remove(const char *device_id) {
    if (!device_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            // Shift remaining devices
            if (i < device_count - 1) {
                memmove(&device_registry[i], &device_registry[i + 1],
                       (device_count - i - 1) * sizeof(tuya_device_t));
            }
            device_count--;
            ESP_LOGI(TAG, "Removed device: %s", device_id);
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_set_state(const char *device_id, tuya_device_state_t state) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].state = state;
            device_registry[i].last_seen = (uint32_t)time(NULL);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_set_switch(const char *device_id, bool on) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].switch_on = on;
            device_registry[i].last_seen = (uint32_t)time(NULL);
            ESP_LOGD(TAG, "Device %s switch: %s", device_id, on ? "ON" : "OFF");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_set_brightness(const char *device_id, uint8_t brightness) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].brightness = brightness;
            device_registry[i].last_seen = (uint32_t)time(NULL);
            ESP_LOGD(TAG, "Device %s brightness: %d%%", device_id, brightness);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_set_contact(const char *device_id, bool open) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].contact_open = open;
            device_registry[i].last_seen = (uint32_t)time(NULL);
            ESP_LOGD(TAG, "Device %s contact: %s", device_id, open ? "OPEN" : "CLOSED");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_set_motion(const char *device_id, bool motion) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].motion_detected = motion;
            device_registry[i].last_seen = (uint32_t)time(NULL);
            ESP_LOGD(TAG, "Device %s motion: %s", device_id, motion ? "DETECTED" : "CLEAR");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_map_to_zone(const char *device_id, int zone_id) {
    if (zone_id != -1 && (zone_id < 67 || zone_id > 96)) {
        ESP_LOGE(TAG, "Invalid zone ID %d (must be 67-96 or -1 for unmap)", zone_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].virtual_zone_id = zone_id;
            ESP_LOGI(TAG, "Device %s mapped to zone %d", device_id, zone_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_devices_map_to_relay(const char *device_id, int relay_id) {
    if (relay_id != -1 && (relay_id < 32 || relay_id > 63)) {
        ESP_LOGE(TAG, "Invalid relay ID %d (must be 32-63 or -1 for unmap)", relay_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_registry[i].device_id, device_id) == 0) {
            device_registry[i].virtual_relay_id = relay_id;
            ESP_LOGI(TAG, "Device %s mapped to relay %d", device_id, relay_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

const char* tuya_device_type_name(tuya_device_type_t type) {
    switch (type) {
        case TUYA_DEV_TYPE_SWITCH:        return "Switch";
        case TUYA_DEV_TYPE_LIGHT:         return "Light";
        case TUYA_DEV_TYPE_RGB_LIGHT:     return "RGB Light";
        case TUYA_DEV_TYPE_DOOR_SENSOR:   return "Door Sensor";
        case TUYA_DEV_TYPE_MOTION_SENSOR: return "Motion Sensor";
        case TUYA_DEV_TYPE_TEMP_SENSOR:   return "Temperature Sensor";
        case TUYA_DEV_TYPE_LOCK:          return "Lock";
        case TUYA_DEV_TYPE_CURTAIN:       return "Curtain";
        case TUYA_DEV_TYPE_SOCKET:        return "Socket";
        case TUYA_DEV_TYPE_THERMOSTAT:    return "Thermostat";
        default:                          return "Unknown";
    }
}

esp_err_t tuya_devices_load(void) {
    // TODO: Implement loading from /spiffs/tuya.json or /sd/tuya.json
    ESP_LOGW(TAG, "Device loading not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_devices_save(void) {
    // TODO: Implement saving to /spiffs/tuya.json or /sd/tuya.json
    ESP_LOGW(TAG, "Device saving not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}
