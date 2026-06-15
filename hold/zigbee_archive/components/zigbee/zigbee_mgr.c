/**
 * @file zigbee_mgr.c
 * @brief Zigbee manager implementation
 */

#include "zigbee_mgr.h"
#include "zigbee_protocol.h"
#include "zigbee_devices.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "ZIGBEE_MGR";

// Manager state
static struct {
    bool initialized;
    zigbee_event_callback_t event_callback;
    TimerHandle_t heartbeat_timer;
    uint32_t start_time;           // For uptime calculation
    uint32_t commands_sent;
    uint32_t commands_failed;
    uint32_t frames_received;
} s_mgr;

// Forward declarations
static void frame_callback(const zigbee_frame_t *frame);
static void heartbeat_timer_cb(TimerHandle_t timer);

/**
 * Initialize manager
 */
esp_err_t zigbee_mgr_init(const void *config, zigbee_event_callback_t callback) {
    if (s_mgr.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.event_callback = callback;

    // Initialize device registry
    zigbee_devices_init();

    // Initialize protocol
    esp_err_t ret = zigbee_protocol_init((const zigbee_config_t *)config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Protocol init failed: %d", ret);
        return ret;
    }

    // Set frame callback
    zigbee_protocol_set_callback(frame_callback);

    // Create heartbeat timer (30 second interval)
    s_mgr.heartbeat_timer = xTimerCreate("zigbee_hb", pdMS_TO_TICKS(30000),
                                         pdTRUE, NULL, heartbeat_timer_cb);
    if (s_mgr.heartbeat_timer) {
        xTimerStart(s_mgr.heartbeat_timer, 0);
    }

    s_mgr.initialized = true;

    ESP_LOGI(TAG, "Zigbee manager initialized");
    
    // Trigger initial discovery
    zigbee_mgr_discover_devices(5000);

    return ESP_OK;
}

/**
 * Deinitialize
 */
void zigbee_mgr_deinit(void) {
    if (!s_mgr.initialized) {
        return;
    }

    if (s_mgr.heartbeat_timer) {
        xTimerStop(s_mgr.heartbeat_timer, 0);
        xTimerDelete(s_mgr.heartbeat_timer, 0);
    }

    zigbee_protocol_deinit();
    s_mgr.initialized = false;

    ESP_LOGI(TAG, "Zigbee manager deinitialized");
}

/**
 * Check connection
 */
bool zigbee_mgr_is_connected(void) {
    return s_mgr.initialized && zigbee_protocol_is_connected();
}

/**
 * Enable pairing
 */
esp_err_t zigbee_mgr_enable_pairing(uint16_t duration_sec) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Enabling pairing for %d seconds", duration_sec);
    
    uint8_t duration = (duration_sec > 255) ? 255 : duration_sec;
    return zigbee_protocol_permit_join(duration);
}

/**
 * Discover devices
 */
esp_err_t zigbee_mgr_discover_devices(uint32_t timeout_ms) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Discovering devices (timeout=%u ms)", timeout_ms);
    return zigbee_protocol_discover_devices();
}

/**
 * Get all devices
 */
esp_err_t zigbee_mgr_get_devices(zigbee_device_t *devices, int *count) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return zigbee_devices_get_all(devices, count);
}

/**
 * Get device
 */
const zigbee_device_t* zigbee_mgr_get_device(uint16_t short_addr) {
    if (!s_mgr.initialized) {
        return NULL;
    }

    return zigbee_devices_get(short_addr);
}

/**
 * Update device configuration
 */
esp_err_t zigbee_mgr_update_device(uint16_t short_addr, const char *name,
                                    int zone_id, int relay_id, int enabled) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *device = zigbee_devices_get(short_addr);
    if (!device) {
        return ESP_ERR_NOT_FOUND;
    }

    // Update fields if provided (cast away const for modification)
    zigbee_device_t *mutable_device = (zigbee_device_t *)device;
    
    if (name != NULL) {
        strncpy(mutable_device->name, name, ZIGBEE_MAX_NAME_LEN - 1);
        mutable_device->name[ZIGBEE_MAX_NAME_LEN - 1] = '\0';
    }

    if (zone_id >= 0) {
        mutable_device->zone_id = zone_id;
    }

    if (relay_id >= 0) {
        mutable_device->relay_id = relay_id;
    }

    // Note: zigbee_device_t doesn't have 'enabled' field in struct
    // If needed, add it to zigbee_devices.h or use state field

    ESP_LOGI(TAG, "Updated device 0x%04X: name=%s, zone=%d, relay=%d", 
             short_addr, name ? name : mutable_device->name, mutable_device->zone_id, mutable_device->relay_id);

    return ESP_OK;
}

/**
 * Remove device
 */
esp_err_t zigbee_mgr_remove_device(uint16_t short_addr) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Removing device 0x%04X", short_addr);

    // TODO: Send leave request to device

    return zigbee_devices_remove(short_addr);
}

/**
 * Set on/off
 */
esp_err_t zigbee_mgr_set_on_off(uint16_t short_addr, bool on) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *dev = zigbee_devices_get(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Setting device 0x%04X to %s", short_addr, on ? "ON" : "OFF");

    // Send command
    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_on_off(short_addr, dev->endpoint, on);
    if (ret == ESP_OK) {
        // Update local state
        zigbee_devices_set_on_off(short_addr, on);
    } else {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Set level (brightness)
 */
esp_err_t zigbee_mgr_set_level(uint16_t short_addr, uint8_t level) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (level > 100) {
        level = 100;
    }

    const zigbee_device_t *dev = zigbee_devices_get(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Setting device 0x%04X level to %d%%", short_addr, level);

    // Convert 0-100% to 0-254
    uint8_t zigbee_level = (level * 254) / 100;

    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_level(short_addr, dev->endpoint, zigbee_level);
    if (ret == ESP_OK) {
        zigbee_devices_set_level(short_addr, zigbee_level);
    } else {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Set color (RGB to Hue/Saturation conversion)
 */
esp_err_t zigbee_mgr_set_color(uint16_t short_addr, uint32_t rgb) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!(dev->capabilities & ZIGBEE_CAP_COLOR)) {
        ESP_LOGW(TAG, "Device 0x%04X does not support color control", short_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Extract RGB components
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    // Convert RGB to HSV
    float r_f = r / 255.0f;
    float g_f = g / 255.0f;
    float b_f = b / 255.0f;

    float max = (r_f > g_f) ? ((r_f > b_f) ? r_f : b_f) : ((g_f > b_f) ? g_f : b_f);
    float min = (r_f < g_f) ? ((r_f < b_f) ? r_f : b_f) : ((g_f < b_f) ? g_f : b_f);
    float delta = max - min;

    float hue = 0.0f;
    float saturation = (max == 0.0f) ? 0.0f : (delta / max);

    if (delta > 0.0f) {
        if (max == r_f) {
            hue = 60.0f * fmodf(((g_f - b_f) / delta), 6.0f);
        } else if (max == g_f) {
            hue = 60.0f * (((b_f - r_f) / delta) + 2.0f);
        } else {
            hue = 60.0f * (((r_f - g_f) / delta) + 4.0f);
        }
        if (hue < 0.0f) hue += 360.0f;
    }

    // Convert to Zigbee format (0-254)
    uint8_t zigbee_hue = (uint8_t)((hue / 360.0f) * 254.0f);
    uint8_t zigbee_sat = (uint8_t)(saturation * 254.0f);

    ESP_LOGI(TAG, "Setting device 0x%04X color: RGB(0x%06X) -> H:%d S:%d",
             short_addr, rgb, zigbee_hue, zigbee_sat);

    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_color(short_addr, dev->endpoint, zigbee_hue, zigbee_sat);
    if (ret == ESP_OK) {
        zigbee_devices_set_color(short_addr, rgb);
    } else {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Set color temperature
 */
esp_err_t zigbee_mgr_set_color_temp(uint16_t short_addr, uint16_t mireds) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!(dev->capabilities & ZIGBEE_CAP_COLOR)) {
        ESP_LOGW(TAG, "Device 0x%04X does not support color temperature", short_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Zigbee color temp range: typically 153-500 mireds (6500K-2000K)
    if (mireds < 153 || mireds > 500) {
        ESP_LOGW(TAG, "Color temperature %d out of range (153-500 mireds)", mireds);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting device 0x%04X color temp to %d mireds (~%dK)",
             short_addr, mireds, 1000000 / mireds);

    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_color_temp(short_addr, dev->endpoint, mireds);
    if (ret == ESP_OK) {
        zigbee_devices_set_color_temp(short_addr, mireds);
    } else {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Set lock/unlock
 */
esp_err_t zigbee_mgr_set_lock(uint16_t short_addr, bool locked) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!(dev->capabilities & ZIGBEE_CAP_LOCK)) {
        ESP_LOGW(TAG, "Device 0x%04X is not a lock", short_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Setting device 0x%04X lock state: %s", short_addr, locked ? "LOCKED" : "UNLOCKED");

    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_lock(short_addr, dev->endpoint, locked);
    if (ret != ESP_OK) {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Map to zone
 */
esp_err_t zigbee_mgr_map_to_zone(uint16_t short_addr, int zone_id) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return zigbee_devices_map_to_zone(short_addr, zone_id);
}

/**
 * Map to relay
 */
esp_err_t zigbee_mgr_map_to_relay(uint16_t short_addr, int relay_id) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return zigbee_devices_map_to_relay(short_addr, relay_id);
}

/**
 * Get zone state
 */
bool zigbee_mgr_get_zone_state(int zone_id) {
    // Find device mapped to this zone
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int count = 0;

    zigbee_devices_get_all(devices, &count);

    for (int i = 0; i < count; i++) {
        if (devices[i].zone_id == zone_id) {
            // Return sensor state based on device type
            if (devices[i].capabilities & ZIGBEE_CAP_CONTACT) {
                return devices[i].current_contact;  // true = open (triggered)
            }
            if (devices[i].capabilities & ZIGBEE_CAP_OCCUPANCY) {
                return devices[i].current_occupancy;  // true = motion detected
            }
        }
    }

    return false;  // No device or not triggered
}

/**
 * Set relay state
 */
esp_err_t zigbee_mgr_set_relay_state(int relay_id, bool on) {
    // Find device mapped to this relay
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int count = 0;

    zigbee_devices_get_all(devices, &count);

    for (int i = 0; i < count; i++) {
        if (devices[i].relay_id == relay_id) {
            return zigbee_mgr_set_on_off(devices[i].short_addr, on);
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * Frame callback - handles incoming frames from coordinator
 */
static void frame_callback(const zigbee_frame_t *frame) {
    if (!frame) {
        return;
    }

    s_mgr.frames_received++;

    ESP_LOGD(TAG, "Received frame from 0x%04X, cluster=0x%04X, cmd=0x%02X",
             frame->short_addr, frame->cluster_id, frame->command_id);

    // Process frame based on cluster
    switch (frame->cluster_id) {
        case ZIGBEE_CLUSTER_ON_OFF:
            // On/off state update
            if (frame->payload && frame->payload_len > 0) {
                bool on_off = frame->payload[0] != 0;
                zigbee_devices_set_on_off(frame->short_addr, on_off);
            }
            break;

        case ZIGBEE_CLUSTER_IAS_ZONE:
            // Security sensor update (door/motion/smoke)
            // TODO: Parse IAS zone status
            break;

        default:
            ESP_LOGD(TAG, "Unhandled cluster 0x%04X", frame->cluster_id);
            break;
    }
}

/**
 * Set thermostat temperature setpoint
 */
esp_err_t zigbee_mgr_set_thermostat(uint16_t short_addr, float temperature) {
    if (!s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    if (dev->type != ZIGBEE_DEVICE_THERMOSTAT) {
        ESP_LOGW(TAG, "Device 0x%04X is not a thermostat", short_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Validate range (10-35°C is typical)
    if (temperature < 10.0f || temperature > 35.0f) {
        ESP_LOGW(TAG, "Temperature %.1f°C out of range (10-35°C)", temperature);
        return ESP_ERR_INVALID_ARG;
    }

    // Convert to Zigbee format (0.01°C units)
    int16_t zigbee_temp = (int16_t)(temperature * 100.0f);

    ESP_LOGI(TAG, "Setting thermostat 0x%04X to %.1f°C", short_addr, temperature);

    s_mgr.commands_sent++;
    esp_err_t ret = zigbee_protocol_send_thermostat_setpoint(short_addr, dev->endpoint, zigbee_temp);
    if (ret != ESP_OK) {
        s_mgr.commands_failed++;
    }

    return ret;
}

/**
 * Get battery level for device
 */
int zigbee_mgr_get_battery_level(uint16_t short_addr) {
    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return -1;
    }

    if (!(dev->capabilities & ZIGBEE_CAP_BATTERY)) {
        return -1;
    }

    return (int)dev->current_battery;
}

/**
 * Check if device battery is low (<20%)
 */
bool zigbee_mgr_is_battery_low(uint16_t short_addr) {
    int level = zigbee_mgr_get_battery_level(short_addr);
    return (level >= 0 && level < 20);
}

/**
 * Get network statistics
 */
esp_err_t zigbee_mgr_get_statistics(zigbee_network_stats_t *stats) {
    if (!s_mgr.initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(zigbee_network_stats_t));

    // Count devices by state
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int count = 0;
    zigbee_mgr_get_devices(devices, &count);

    stats->total_devices = count;

    for (int i = 0; i < count; i++) {
        if (devices[i].state == ZIGBEE_STATE_ONLINE) {
            stats->online_devices++;
        } else {
            stats->offline_devices++;
        }

        // Check battery
        if ((devices[i].capabilities & ZIGBEE_CAP_BATTERY) && devices[i].current_battery < 20) {
            stats->low_battery_devices++;
        }
    }

    // Manager stats
    stats->commands_sent = s_mgr.commands_sent;
    stats->commands_failed = s_mgr.commands_failed;
    stats->frames_received = s_mgr.frames_received;
    stats->uptime_seconds = (xTaskGetTickCount() - s_mgr.start_time) / 1000;

    return ESP_OK;
}

/**
 * Heartbeat timer - periodic network maintenance
 */
static void heartbeat_timer_cb(TimerHandle_t timer) {
    if (!s_mgr.initialized) {
        return;
    }

    ESP_LOGD(TAG, "Heartbeat - %d devices", zigbee_devices_get_count());

    // Check for low battery devices
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int count = 0;
    zigbee_mgr_get_devices(devices, &count);

    for (int i = 0; i < count; i++) {
        if (zigbee_mgr_is_battery_low(devices[i].short_addr)) {
            ESP_LOGW(TAG, "Device 0x%04X (%s) has low battery: %d%%",
                     devices[i].short_addr, devices[i].name, devices[i].current_battery);
        }
    }
    // - Check device online status
    // - Update link quality
    // - Refresh device states
}
