/**
 * @file matter_clusters.c
 * @brief Matter cluster implementations for common device types
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "esp_matter_controller_client.h"
#include <string.h>

static const char *TAG = "MATTER_CLUSTERS";

// Matter cluster IDs (from Matter specification)
#define MATTER_CLUSTER_ON_OFF           0x0006
#define MATTER_CLUSTER_LEVEL_CONTROL    0x0008
#define MATTER_CLUSTER_DOOR_LOCK        0x0101
#define MATTER_CLUSTER_OCCUPANCY        0x0406
#define MATTER_CLUSTER_BOOLEAN_STATE    0x0045
#define MATTER_CLUSTER_TEMPERATURE      0x0402
#define MATTER_CLUSTER_SMOKE_CO_ALARM   0x005C

// Matter attribute IDs
#define ATTR_ON_OFF                     0x0000
#define ATTR_CURRENT_LEVEL              0x0000
#define ATTR_LOCK_STATE                 0x0000
#define ATTR_OCCUPANCY                  0x0000
#define ATTR_STATE_VALUE                0x0000
#define ATTR_MEASURED_VALUE             0x0000

// Forward declaration
extern esp_matter_controller_t *g_controller;
extern void trigger_alarm(int zone_id, const char *reason);
extern int engine_get_arm_state(void);

/**
 * @brief Callback for occupancy sensor attribute reports
 */
static void _occupancy_report_callback(uint64_t node_id, uint16_t endpoint_id, 
                                       uint32_t cluster_id, uint32_t attr_id, 
                                       void *value, size_t value_len) {
    if (cluster_id != MATTER_CLUSTER_OCCUPANCY || attr_id != ATTR_OCCUPANCY) {
        return;
    }
    
    bool occupied = *(bool*)value;
    
    ESP_LOGI(TAG, "Occupancy sensor 0x%llX: %s", node_id, occupied ? "OCCUPIED" : "CLEAR");
    
    // Find mapped virtual zone
    // TODO: Look up zone mapping and trigger if armed
    
    // Example: If mapped to zone 33 and system is armed
    if (occupied && engine_get_arm_state() >= 2) {
        trigger_alarm(33, "Matter Motion Detected");
    }
}

/**
 * @brief Callback for contact sensor attribute reports
 */
static void _contact_report_callback(uint64_t node_id, uint16_t endpoint_id,
                                     uint32_t cluster_id, uint32_t attr_id,
                                     void *value, size_t value_len) {
    if (cluster_id != MATTER_CLUSTER_BOOLEAN_STATE || attr_id != ATTR_STATE_VALUE) {
        return;
    }
    
    bool state = *(bool*)value;
    
    ESP_LOGI(TAG, "Contact sensor 0x%llX: %s", node_id, state ? "OPEN" : "CLOSED");
    
    // Trigger alarm if door/window opened while armed
    if (state && engine_get_arm_state() >= 2) {
        trigger_alarm(34, "Matter Door/Window Opened");
    }
}

/**
 * @brief Subscribe to occupancy sensor
 */
int matter_subscribe_occupancy_sensor(uint64_t node_id, uint16_t endpoint_id) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Subscribing to occupancy sensor 0x%llX endpoint %d", node_id, endpoint_id);
    
    esp_matter_controller_subscribe_config_t sub_config = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = MATTER_CLUSTER_OCCUPANCY,
        .attribute_id = ATTR_OCCUPANCY,
        .min_interval_sec = 0,
        .max_interval_sec = 10,
        .callback = _occupancy_report_callback,
    };
    
    esp_err_t err = esp_matter_controller_subscribe_attribute(g_controller, &sub_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Subscription failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Subscribe to contact sensor
 */
int matter_subscribe_contact_sensor(uint64_t node_id, uint16_t endpoint_id) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Subscribing to contact sensor 0x%llX endpoint %d", node_id, endpoint_id);
    
    esp_matter_controller_subscribe_config_t sub_config = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = MATTER_CLUSTER_BOOLEAN_STATE,
        .attribute_id = ATTR_STATE_VALUE,
        .min_interval_sec = 0,
        .max_interval_sec = 5,
        .callback = _contact_report_callback,
    };
    
    esp_err_t err = esp_matter_controller_subscribe_attribute(g_controller, &sub_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Subscription failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Send on/off command to Matter device
 */
int matter_cluster_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Sending %s to 0x%llX endpoint %d", on ? "ON" : "OFF", node_id, endpoint_id);
    
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = MATTER_CLUSTER_ON_OFF,
        .command_id = on ? 0x01 : 0x00,  // On=1, Off=0, Toggle=2
        .timeout_ms = 5000,
    };
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "On/Off command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Send level control command (brightness)
 */
int matter_cluster_send_level(uint64_t node_id, uint16_t endpoint_id, uint8_t level, uint16_t transition_ms) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Setting level to %d on 0x%llX endpoint %d", level, node_id, endpoint_id);
    
    // MoveToLevel command (0x04)
    // Parameters: level (uint8), transition_time (uint16), option_mask (uint8), option_override (uint8)
    
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = MATTER_CLUSTER_LEVEL_CONTROL,
        .command_id = 0x04,  // MoveToLevel
        .timeout_ms = 5000,
    };
    
    // TODO: Encode command parameters
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Level control command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Send door lock command
 */
int matter_cluster_send_lock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code) {
    if (!g_controller) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Sending %s to 0x%llX endpoint %d", lock ? "LOCK" : "UNLOCK", node_id, endpoint_id);
    
    esp_matter_controller_command_config_t cmd = {
        .node_id = node_id,
        .endpoint_id = endpoint_id,
        .cluster_id = MATTER_CLUSTER_DOOR_LOCK,
        .command_id = lock ? 0x00 : 0x01,  // LockDoor=0, UnlockDoor=1
        .timeout_ms = 5000,
    };
    
    // TODO: Encode PIN code if provided
    
    esp_err_t err = esp_matter_controller_send_command(g_controller, &cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Door lock command failed: %d", err);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Read temperature from sensor
 */
int matter_cluster_read_temperature(uint64_t node_id, uint16_t endpoint_id, float *temperature) {
    if (!g_controller || !temperature) {
        return -1;
    }
    
    // Read MeasuredValue attribute from Temperature Measurement cluster
    // TODO: Implement attribute read
    
    *temperature = 0.0f;
    return 0;
}

/**
 * @brief Discover device clusters and capabilities
 */
int matter_cluster_discover_device(uint64_t node_id, uint16_t endpoint_id, matter_device_type_t *device_type) {
    if (!g_controller || !device_type) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Discovering device 0x%llX endpoint %d", node_id, endpoint_id);
    
    // Read device type from descriptor cluster
    // Determine if it's a light, sensor, lock, etc.
    
    // TODO: Implement cluster discovery
    
    *device_type = MATTER_DEVICE_UNKNOWN;
    return 0;
}
