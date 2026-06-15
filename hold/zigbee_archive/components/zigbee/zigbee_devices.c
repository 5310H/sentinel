/**
 * @file zigbee_devices.c
 * @brief Zigbee device registry implementation
 */

#include "zigbee_devices.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ZIGBEE_DEV";

// Device registry
static struct {
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int count;
} s_registry;

/**
 * Initialize device registry
 */
void zigbee_devices_init(void) {
    memset(&s_registry, 0, sizeof(s_registry));
    ESP_LOGI(TAG, "Device registry initialized");
}

/**
 * Add or update device
 */
esp_err_t zigbee_devices_add_or_update(const zigbee_device_t *device) {
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if device already exists (by short address)
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == device->short_addr) {
            // Update existing device
            memcpy(&s_registry.devices[i], device, sizeof(zigbee_device_t));
            ESP_LOGD(TAG, "Updated device 0x%04X: %s", device->short_addr, device->name);
            return ESP_OK;
        }
    }

    // Add new device
    if (s_registry.count >= ZIGBEE_MAX_DEVICES) {
        ESP_LOGE(TAG, "Device registry full");
        return ESP_ERR_NO_MEM;
    }

    memcpy(&s_registry.devices[s_registry.count], device, sizeof(zigbee_device_t));
    s_registry.count++;

    ESP_LOGI(TAG, "Added device 0x%04X: %s (type=%d)", 
             device->short_addr, device->name, device->type);

    return ESP_OK;
}

/**
 * Remove device
 */
esp_err_t zigbee_devices_remove(uint16_t short_addr) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            ESP_LOGI(TAG, "Removing device 0x%04X: %s", 
                     short_addr, s_registry.devices[i].name);

            // Shift remaining devices
            for (int j = i; j < s_registry.count - 1; j++) {
                memcpy(&s_registry.devices[j], &s_registry.devices[j + 1], 
                       sizeof(zigbee_device_t));
            }

            s_registry.count--;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * Get device by short address
 */
const zigbee_device_t* zigbee_devices_get(uint16_t short_addr) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            return &s_registry.devices[i];
        }
    }
    return NULL;
}

/**
 * Get device by IEEE address
 */
const zigbee_device_t* zigbee_devices_get_by_ieee(const uint8_t *ieee_addr) {
    if (!ieee_addr) {
        return NULL;
    }

    for (int i = 0; i < s_registry.count; i++) {
        if (memcmp(s_registry.devices[i].ieee_addr, ieee_addr, ZIGBEE_IEEE_ADDR_LEN) == 0) {
            return &s_registry.devices[i];
        }
    }
    return NULL;
}

/**
 * Get all devices
 */
esp_err_t zigbee_devices_get_all(zigbee_device_t *devices, int *count) {
    if (!devices || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(devices, s_registry.devices, s_registry.count * sizeof(zigbee_device_t));
    *count = s_registry.count;

    return ESP_OK;
}

/**
 * Get device count
 */
int zigbee_devices_get_count(void) {
    return s_registry.count;
}

/**
 * Update on/off state
 */
esp_err_t zigbee_devices_set_on_off(uint16_t short_addr, bool on_off) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].current_on_off = on_off;
            ESP_LOGD(TAG, "Device 0x%04X on/off = %d", short_addr, on_off);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Update level
 */
esp_err_t zigbee_devices_set_level(uint16_t short_addr, uint8_t level) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].current_level = level;
            ESP_LOGD(TAG, "Device 0x%04X level = %d", short_addr, level);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Set color
 */
esp_err_t zigbee_devices_set_color(uint16_t short_addr, uint32_t rgb) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].current_color_rgb = rgb;
            ESP_LOGD(TAG, "Device 0x%04X color = 0x%06X", short_addr, rgb);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Set color temperature
 */
esp_err_t zigbee_devices_set_color_temp(uint16_t short_addr, uint16_t mireds) {
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].current_color_temp = mireds;
            ESP_LOGD(TAG, "Device 0x%04X color temp = %d mireds", short_addr, mireds);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Map to zone
 */
esp_err_t zigbee_devices_map_to_zone(uint16_t short_addr, int zone_id) {
    // Validate zone range (50-64)
    if (zone_id != 0 && (zone_id < 50 || zone_id > 64)) {
        ESP_LOGE(TAG, "Invalid zone ID %d (must be 50-64)", zone_id);
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].zone_id = zone_id;
            ESP_LOGI(TAG, "Device 0x%04X mapped to zone %d", short_addr, zone_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Map to relay
 */
esp_err_t zigbee_devices_map_to_relay(uint16_t short_addr, int relay_id) {
    // Validate relay range (64-95)
    if (relay_id != 0 && (relay_id < 64 || relay_id > 95)) {
        ESP_LOGE(TAG, "Invalid relay ID %d (must be 64-95)", relay_id);
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].short_addr == short_addr) {
            s_registry.devices[i].relay_id = relay_id;
            ESP_LOGI(TAG, "Device 0x%04X mapped to relay %d", short_addr, relay_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * Get device type name
 */
const char* zigbee_device_type_name(zigbee_device_type_t type) {
    switch (type) {
        case ZIGBEE_DEVICE_SWITCH: return "switch";
        case ZIGBEE_DEVICE_LIGHT: return "light";
        case ZIGBEE_DEVICE_COLOR_LIGHT: return "color_light";
        case ZIGBEE_DEVICE_OUTLET: return "outlet";
        case ZIGBEE_DEVICE_DOOR_SENSOR: return "door_sensor";
        case ZIGBEE_DEVICE_MOTION_SENSOR: return "motion_sensor";
        case ZIGBEE_DEVICE_TEMPERATURE: return "temperature";
        case ZIGBEE_DEVICE_HUMIDITY: return "humidity";
        case ZIGBEE_DEVICE_LEAK_SENSOR: return "leak_sensor";
        case ZIGBEE_DEVICE_SMOKE_DETECTOR: return "smoke_detector";
        case ZIGBEE_DEVICE_LOCK: return "lock";
        case ZIGBEE_DEVICE_THERMOSTAT: return "thermostat";
        case ZIGBEE_DEVICE_BUTTON: return "button";
        case ZIGBEE_DEVICE_DIMMER: return "dimmer";
        case ZIGBEE_DEVICE_BLIND: return "blind";
        default: return "unknown";
    }
}
