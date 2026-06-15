/**
 * @file matter_controller.c
 * @brief Matter controller implementation
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_controller_client.h"
#include "esp_matter_controller_utils.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "MATTER_CTRL";

// Global device registry
static matter_device_t g_devices[MAX_MATTER_DEVICES];
static int g_device_count = 0;
static SemaphoreHandle_t g_device_mutex = NULL;

// Matter controller instance
static esp_matter_controller_t *g_controller = NULL;

// Forward declarations
extern int engine_get_arm_state(void);
extern void monitor_set_zone_state(int zone, bool state);
extern void trigger_alarm(int zone_id, const char *reason);

/**
 * @brief Initialize Matter controller
 */
int matter_controller_init(void) {
    ESP_LOGI(TAG, "Initializing Matter controller...");
    
    // Create mutex for device registry
    if (g_device_mutex == NULL) {
        g_device_mutex = xSemaphoreCreateMutex();
        if (g_device_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create device mutex");
            return -1;
        }
    }
    
    // Initialize Matter stack
    esp_matter_controller_config_t config = {
        .storage_namespace = "matter_ctrl",
        .auto_reconnect = true,
        .max_connections = 16,
    };
    
    esp_err_t err = esp_matter_controller_init(&config, &g_controller);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter controller init failed: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Matter controller initialized successfully");
    
    // Load saved device configuration
    matter_load_config();
    
    return 0;
}

/**
 * @brief Find device by node ID
 */
static matter_device_t* _find_device(uint64_t node_id) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].node_id == node_id) {
            return &g_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief Add device to registry
 */
static matter_device_t* _add_device(uint64_t node_id, const char *name) {
    if (g_device_count >= MAX_MATTER_DEVICES) {
        ESP_LOGE(TAG, "Device registry full");
        return NULL;
    }
    
    matter_device_t *dev = &g_devices[g_device_count++];
    memset(dev, 0, sizeof(matter_device_t));
    
    dev->node_id = node_id;
    dev->endpoint_id = 1;  // Default endpoint
    dev->device_type = MATTER_DEVICE_UNKNOWN;
    dev->is_commissioned = true;
    dev->is_online = false;
    dev->virtual_zone_id = -1;
    dev->virtual_relay_id = -1;
    
    if (name) {
        strncpy(dev->name, name, sizeof(dev->name) - 1);
    } else {
        snprintf(dev->name, sizeof(dev->name), "Device_%llX", node_id);
    }
    
    return dev;
}

/**
 * @brief Commission new Matter device
 */
int matter_commission_device(const char *pairing_code, const char *device_name) {
    if (!g_controller || !pairing_code) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioning device: %s", device_name ? device_name : "Unknown");
    
    xSemaphoreTake(g_device_mutex, portMAX_DELAY);
    
    // Parse pairing code and commission device
    // Format: MT:Y.K9QAO00KNY0648G00 (manual pairing code)
    // or numeric QR code payload
    
    esp_matter_controller_commission_config_t comm_config = {
        .pairing_code = pairing_code,
        .discovery_type = ESP_MATTER_CONTROLLER_DISCOVERY_ON_NETWORK,
    };
    
    uint64_t node_id = 0;
    esp_err_t err = esp_matter_controller_commission_device(g_controller, &comm_config, &node_id);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Commissioning failed: %d", err);
        xSemaphoreGive(g_device_mutex);
        return -1;
    }
    
    // Add to device registry
    matter_device_t *dev = _add_device(node_id, device_name);
    if (!dev) {
        xSemaphoreGive(g_device_mutex);
        return -1;
    }
    
    ESP_LOGI(TAG, "Device commissioned successfully: Node ID 0x%llX", node_id);
    
    // Save configuration
    matter_save_config();
    
    xSemaphoreGive(g_device_mutex);
    
    return 0;
}

/**
 * @brief Remove commissioned device
 */
int matter_remove_device(uint64_t node_id) {
    if (!g_controller) {
        return -1;
    }
    
    xSemaphoreTake(g_device_mutex, portMAX_DELAY);
    
    // Find device in registry
    matter_device_t *dev = _find_device(node_id);
    if (!dev) {
        ESP_LOGE(TAG, "Device not found: 0x%llX", node_id);
        xSemaphoreGive(g_device_mutex);
        return -1;
    }
    
    // Remove from Matter fabric
    esp_err_t err = esp_matter_controller_unpair_device(g_controller, node_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unpair failed (device may be offline): %d", err);
    }
    
    // Remove from registry (shift array)
    int idx = dev - g_devices;
    for (int i = idx; i < g_device_count - 1; i++) {
        g_devices[i] = g_devices[i + 1];
    }
    g_device_count--;
    
    ESP_LOGI(TAG, "Device removed: 0x%llX", node_id);
    
    // Save configuration
    matter_save_config();
    
    xSemaphoreGive(g_device_mutex);
    
    return 0;
}

/**
 * @brief Get list of all devices
 */
int matter_get_devices(matter_device_t *devices, int max_devices, int *count) {
    if (!devices || !count) {
        return -1;
    }
    
    xSemaphoreTake(g_device_mutex, portMAX_DELAY);
    
    int copy_count = (g_device_count < max_devices) ? g_device_count : max_devices;
    memcpy(devices, g_devices, copy_count * sizeof(matter_device_t));
    *count = copy_count;
    
    xSemaphoreGive(g_device_mutex);
    
    return 0;
}

/**
 * @brief Map sensor to virtual zone
 */
int matter_map_sensor_to_zone(uint64_t node_id, uint16_t endpoint_id, int zone_id) {
    if (zone_id < 33 || zone_id > 96) {
        ESP_LOGE(TAG, "Invalid zone ID: %d (must be 33-96)", zone_id);
        return -1;
    }
    
    xSemaphoreTake(g_device_mutex, portMAX_DELAY);
    
    matter_device_t *dev = _find_device(node_id);
    if (!dev) {
        ESP_LOGE(TAG, "Device not found: 0x%llX", node_id);
        xSemaphoreGive(g_device_mutex);
        return -1;
    }
    
    dev->endpoint_id = endpoint_id;
    dev->virtual_zone_id = zone_id;
    
    ESP_LOGI(TAG, "Mapped %s to zone %d", dev->name, zone_id);
    
    matter_save_config();
    
    xSemaphoreGive(g_device_mutex);
    
    return 0;
}

/**
 * @brief Map switch to virtual relay
 */
int matter_map_switch_to_relay(uint64_t node_id, uint16_t endpoint_id, int relay_id) {
    if (relay_id < 8 || relay_id > 31) {
        ESP_LOGE(TAG, "Invalid relay ID: %d (must be 8-31)", relay_id);
        return -1;
    }
    
    xSemaphoreTake(g_device_mutex, portMAX_DELAY);
    
    matter_device_t *dev = _find_device(node_id);
    if (!dev) {
        ESP_LOGE(TAG, "Device not found: 0x%llX", node_id);
        xSemaphoreGive(g_device_mutex);
        return -1;
    }
    
    dev->endpoint_id = endpoint_id;
    dev->virtual_relay_id = relay_id;
    
    ESP_LOGI(TAG, "Mapped %s to relay %d", dev->name, relay_id);
    
    matter_save_config();
    
    xSemaphoreGive(g_device_mutex);
    
    return 0;
}

/**
 * @brief Control on/off device
 */
int matter_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Sending %s to node 0x%llX endpoint %d", on ? "ON" : "OFF", node_id, endpoint_id);
    
    // Send Matter On/Off cluster command
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = 0x0006,  // On/Off cluster
        .command_id = on ? 0x01 : 0x00,  // On=1, Off=0
    };
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Set brightness level
 */
int matter_send_brightness(uint64_t node_id, uint16_t endpoint_id, uint8_t level) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Setting brightness to %d on node 0x%llX", level, node_id);
    
    // Send Matter Level Control cluster command
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = 0x0008,  // Level Control cluster
        .command_id = 0x04,    // MoveToLevel command
    };
    
    // TODO: Add command data (level, transition time)
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Lock/unlock door lock
 */
int matter_send_lock_unlock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Sending %s to node 0x%llX", lock ? "LOCK" : "UNLOCK", node_id);
    
    // Send Matter Door Lock cluster command
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = 0x0101,  // Door Lock cluster
        .command_id = lock ? 0x00 : 0x01,  // LockDoor=0, UnlockDoor=1
    };
    
    // TODO: Add PIN code if provided
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Read sensor state
 */
int matter_read_sensor_state(uint64_t node_id, uint16_t endpoint_id, bool *state) {
    if (!g_controller || !state) {
        return -1;
    }
    
    // Read Matter attribute (occupancy or contact)
    // TODO: Implement attribute read
    
    *state = false;
    return 0;
}

/**
 * @brief Subscribe to sensor updates
 */
int matter_subscribe_sensor(uint64_t node_id, uint16_t endpoint_id, matter_sensor_callback_t callback) {
    if (!g_controller || !callback) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Subscribing to sensor on node 0x%llX endpoint %d", node_id, endpoint_id);
    
    // Set up Matter subscription
    // TODO: Implement subscription with callback
    
    return 0;
}

/**
 * @brief Matter event handler task
 */
static void matter_event_task(void *pvParameters) {
    ESP_LOGI(TAG, "Matter event task started");
    
    while (1) {
        // Process Matter events
        // Handle attribute reports, device online/offline, etc.
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Start Matter controller task
 */
int matter_controller_start_task(void) {
    xTaskCreate(matter_event_task, "matter_evt", 8192, NULL, 5, NULL);
    return 0;
}

/**
 * @brief Save configuration to NVS
 */
int matter_save_config(void) {
    ESP_LOGI(TAG, "Saving Matter configuration...");
    
    // TODO: Save device registry to NVS
    // Store: node_id, name, zone/relay mappings
    
    return 0;
}

/**
 * @brief Load configuration from NVS
 */
int matter_load_config(void) {
    ESP_LOGI(TAG, "Loading Matter configuration...");
    
    // TODO: Load device registry from NVS
    
    return 0;
}
