/**
 * @file tuya_mgr.c
 * @brief Tuya Manager Implementation
 */

#include "tuya_mgr.h"
#include "tuya_protocol.h"
#include "tuya_devices.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "tuya_mgr";

// Manager state
static bool manager_initialized = false;
static bool module_connected = false;
static tuya_event_callback_t event_callback = NULL;
static void *callback_user_data = NULL;
static TimerHandle_t heartbeat_timer = NULL;
static TaskHandle_t rx_task_handle = NULL;
static bool pairing_mode_active = false;

// Forward declarations
static void tuya_heartbeat_timer_cb(TimerHandle_t timer);
static void tuya_rx_task(void *arg);
static void handle_received_frame(const tuya_parsed_frame_t *frame);

esp_err_t tuya_mgr_init(tuya_event_callback_t callback, void *user_data) {
    if (manager_initialized) {
        ESP_LOGW(TAG, "Manager already initialized");
        return ESP_OK;
    }

    event_callback = callback;
    callback_user_data = user_data;

    // Initialize protocol layer
    esp_err_t ret = tuya_protocol_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize protocol: %d", ret);
        return ret;
    }

    // Initialize device registry
    ret = tuya_devices_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device registry: %d", ret);
        tuya_protocol_deinit();
        return ret;
    }

    // Create heartbeat timer (10 second interval)
    heartbeat_timer = xTimerCreate("tuya_hb", 
                                   pdMS_TO_TICKS(10000),
                                   pdTRUE,  // Auto-reload
                                   NULL, 
                                   tuya_heartbeat_timer_cb);
    if (!heartbeat_timer) {
        ESP_LOGE(TAG, "Failed to create heartbeat timer");
        tuya_protocol_deinit();
        return ESP_FAIL;
    }

    // Create RX task
    BaseType_t task_ret = xTaskCreate(tuya_rx_task, 
                                      "tuya_rx",
                                      4096,
                                      NULL,
                                      5,
                                      &rx_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        xTimerDelete(heartbeat_timer, 0);
        tuya_protocol_deinit();
        return ESP_FAIL;
    }

    // Start heartbeat timer
    xTimerStart(heartbeat_timer, 0);

    // Query product info
    tuya_protocol_query_product();

    manager_initialized = true;
    module_connected = false;

    ESP_LOGI(TAG, "Tuya manager initialized");

    return ESP_OK;
}

void tuya_mgr_deinit(void) {
    if (!manager_initialized) {
        return;
    }

    // Stop heartbeat timer
    if (heartbeat_timer) {
        xTimerStop(heartbeat_timer, portMAX_DELAY);
        xTimerDelete(heartbeat_timer, portMAX_DELAY);
        heartbeat_timer = NULL;
    }

    // Stop RX task
    if (rx_task_handle) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
    }

    // Deinitialize protocol
    tuya_protocol_deinit();

    manager_initialized = false;
    module_connected = false;

    ESP_LOGI(TAG, "Tuya manager deinitialized");
}

bool tuya_mgr_is_connected(void) {
    return module_connected;
}

esp_err_t tuya_mgr_discover_devices(uint32_t timeout_ms) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting device discovery (timeout: %lu ms)", timeout_ms);
    
    // TODO: Implement device discovery
    // This would involve querying the Tuya cloud through the module
    // or scanning for devices on local network
    
    ESP_LOGW(TAG, "Device discovery not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_enable_pairing(uint16_t duration_sec) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Enabling pairing mode for %d seconds", duration_sec);
    
    // Reset WiFi to enter pairing mode
    esp_err_t ret = tuya_protocol_reset_wifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter pairing mode");
        return ret;
    }

    pairing_mode_active = true;
    
    // TODO: Create timer to automatically disable pairing after duration
    
    return ESP_OK;
}

esp_err_t tuya_mgr_get_devices(tuya_device_t *devices, int max, int *count) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return tuya_devices_get_all(devices, max, count);
}

esp_err_t tuya_mgr_get_device(const char *device_id, tuya_device_t *device) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return tuya_devices_get_by_id(device_id, device);
}

esp_err_t tuya_mgr_remove_device(const char *device_id) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = tuya_devices_remove(device_id);
    if (ret == ESP_OK && event_callback) {
        event_callback(TUYA_EVENT_DEVICE_REMOVED, device_id, callback_user_data);
    }
    
    return ret;
}

esp_err_t tuya_mgr_set_switch(const char *device_id, bool on) {
    if (!manager_initialized || !module_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // Update local state
    esp_err_t ret = tuya_devices_set_switch(device_id, on);
    if (ret != ESP_OK) {
        return ret;
    }

    // Send command to Tuya module
    // TODO: Implement actual DP command based on device mapping
    tuya_dp_t dp = {
        .dp_id = 1,  // Switch DP (typically DP1)
        .dp_type = TUYA_DP_TYPE_BOOL,
        .dp_len = 1,
        .dp_data = {on ? 0x01 : 0x00}
    };
    
    ret = tuya_protocol_report_status(&dp, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send switch command");
        return ret;
    }

    ESP_LOGI(TAG, "Set device %s switch: %s", device_id, on ? "ON" : "OFF");
    
    return ESP_OK;
}

esp_err_t tuya_mgr_set_brightness(const char *device_id, uint8_t brightness) {
    if (!manager_initialized || !module_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    esp_err_t ret = tuya_devices_set_brightness(device_id, brightness);
    if (ret != ESP_OK) {
        return ret;
    }

    // TODO: Send brightness DP command
    ESP_LOGI(TAG, "Set device %s brightness: %d%%", device_id, brightness);
    
    return ESP_OK;
}

esp_err_t tuya_mgr_set_color(const char *device_id, uint32_t rgb) {
    if (!manager_initialized || !module_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Implement RGB color control
    ESP_LOGW(TAG, "RGB color control not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_set_color_temp(const char *device_id, uint16_t mireds) {
    if (!manager_initialized || !module_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Implement color temperature control
    ESP_LOGW(TAG, "Color temperature control not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_set_lock(const char *device_id, bool lock) {
    if (!manager_initialized || !module_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Implement lock control
    ESP_LOGW(TAG, "Lock control not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tuya_mgr_map_to_zone(const char *device_id, int zone_id) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return tuya_devices_map_to_zone(device_id, zone_id);
}

esp_err_t tuya_mgr_map_to_relay(const char *device_id, int relay_id) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return tuya_devices_map_to_relay(device_id, relay_id);
}

esp_err_t tuya_mgr_get_zone_state(int zone_id, bool *triggered) {
    if (!manager_initialized || !triggered) {
        return ESP_ERR_INVALID_ARG;
    }

    if (zone_id < 67 || zone_id > 96) {
        return ESP_ERR_INVALID_ARG;
    }

    // Find device mapped to this zone
    tuya_device_t devices[TUYA_DEVICE_MAX_COUNT];
    int count;
    
    esp_err_t ret = tuya_devices_get_all(devices, TUYA_DEVICE_MAX_COUNT, &count);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < count; i++) {
        if (devices[i].virtual_zone_id == zone_id) {
            // Return sensor state based on device type
            if (devices[i].type == TUYA_DEV_TYPE_DOOR_SENSOR) {
                *triggered = devices[i].contact_open;
                return ESP_OK;
            } else if (devices[i].type == TUYA_DEV_TYPE_MOTION_SENSOR) {
                *triggered = devices[i].motion_detected;
                return ESP_OK;
            }
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_set_relay_state(int relay_id, bool on) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (relay_id < 32 || relay_id > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    // Find device mapped to this relay
    tuya_device_t devices[TUYA_DEVICE_MAX_COUNT];
    int count;
    
    esp_err_t ret = tuya_devices_get_all(devices, TUYA_DEVICE_MAX_COUNT, &count);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < count; i++) {
        if (devices[i].virtual_relay_id == relay_id) {
            // Control device
            return tuya_mgr_set_switch(devices[i].device_id, on);
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t tuya_mgr_save_config(void) {
    return tuya_devices_save();
}

esp_err_t tuya_mgr_load_config(void) {
    return tuya_devices_load();
}

esp_err_t tuya_mgr_reset_wifi(void) {
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return tuya_protocol_reset_wifi();
}

// Timer callback for heartbeat
static void tuya_heartbeat_timer_cb(TimerHandle_t timer) {
    (void)timer;
    
    if (manager_initialized) {
        esp_err_t ret = tuya_protocol_send_heartbeat();
        if (ret == ESP_OK) {
            if (!module_connected) {
                module_connected = true;
                ESP_LOGI(TAG, "Tuya module connected");
                if (event_callback) {
                    event_callback(TUYA_EVENT_CONNECTION_RESTORED, NULL, callback_user_data);
                }
            }
        } else {
            if (module_connected) {
                module_connected = false;
                ESP_LOGW(TAG, "Tuya module disconnected");
                if (event_callback) {
                    event_callback(TUYA_EVENT_CONNECTION_LOST, NULL, callback_user_data);
                }
            }
        }
    }
}

// RX task to receive frames from Tuya module
static void tuya_rx_task(void *arg) {
    (void)arg;
    
    ESP_LOGI(TAG, "RX task started");
    
    tuya_parsed_frame_t frame;
    
    while (1) {
        esp_err_t ret = tuya_protocol_receive_frame(&frame, 1000);
        
        if (ret == ESP_OK) {
            handle_received_frame(&frame);
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "RX error: %d", ret);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Handle received frame from Tuya module
static void handle_received_frame(const tuya_parsed_frame_t *frame) {
    ESP_LOGD(TAG, "Received frame: cmd=0x%02X, len=%d", frame->command, frame->data_len);
    
    switch (frame->command) {
        case TUYA_CMD_HEARTBEAT:
            // Heartbeat response - module is alive
            module_connected = true;
            break;
            
        case TUYA_CMD_STATUS_QUERY:
            // Cloud querying status - parse and update device states
            // TODO: Parse DP data and update device registry
            break;
            
        case TUYA_CMD_WIFI_STATE:
            // WiFi state notification
            if (frame->data_len > 0) {
                tuya_wifi_status_t status = (tuya_wifi_status_t)frame->data[0];
                ESP_LOGI(TAG, "WiFi status: %d", status);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Unhandled command: 0x%02X", frame->command);
            break;
    }
}
