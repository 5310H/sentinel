/**
 * @file matter_storage.c
 * @brief NVS storage for Matter device configuration
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MATTER_STORAGE";

#define NVS_NAMESPACE "matter_cfg"
#define NVS_KEY_DEVICES "devices"

// Forward declarations
extern matter_device_t g_devices[];
extern int g_device_count;

/**
 * @brief Save Matter device configuration to NVS
 */
int matter_save_config(void) {
    ESP_LOGI(TAG, "Saving Matter configuration (%d devices)...", g_device_count);
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return -1;
    }
    
    // Build JSON array of devices
    cJSON *root = cJSON_CreateArray();
    
    for (int i = 0; i < g_device_count; i++) {
        cJSON *dev_obj = cJSON_CreateObject();
        
        // Store device info
        cJSON_AddNumberToObject(dev_obj, "node_id_hi", (uint32_t)(g_devices[i].node_id >> 32));
        cJSON_AddNumberToObject(dev_obj, "node_id_lo", (uint32_t)(g_devices[i].node_id & 0xFFFFFFFF));
        cJSON_AddNumberToObject(dev_obj, "endpoint_id", g_devices[i].endpoint_id);
        cJSON_AddNumberToObject(dev_obj, "device_type", g_devices[i].device_type);
        cJSON_AddStringToObject(dev_obj, "name", g_devices[i].name);
        cJSON_AddNumberToObject(dev_obj, "virtual_zone_id", g_devices[i].virtual_zone_id);
        cJSON_AddNumberToObject(dev_obj, "virtual_relay_id", g_devices[i].virtual_relay_id);
        cJSON_AddBoolToObject(dev_obj, "is_commissioned", g_devices[i].is_commissioned);
        
        cJSON_AddItemToArray(root, dev_obj);
    }
    
    // Serialize to string
    char *json_str = cJSON_Print(root);
    
    if (json_str) {
        // Save to NVS
        err = nvs_set_str(nvs_handle, NVS_KEY_DEVICES, json_str);
        
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
        }
        
        free(json_str);
    }
    
    cJSON_Delete(root);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Matter configuration saved successfully");
    
    return 0;
}

/**
 * @brief Load Matter device configuration from NVS
 */
int matter_load_config(void) {
    ESP_LOGI(TAG, "Loading Matter configuration...");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved configuration found (first run)");
        return 0;  // Not an error
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return -1;
    }
    
    // Get string length
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_DEVICES, NULL, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "No devices configured");
        return 0;
    }
    
    if (err != ESP_OK || required_size == 0) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Failed to get string size: %d", err);
        return -1;
    }
    
    // Allocate buffer and read string
    char *json_str = malloc(required_size);
    if (!json_str) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Failed to allocate memory");
        return -1;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_DEVICES, json_str, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        free(json_str);
        ESP_LOGE(TAG, "Failed to read configuration: %d", err);
        return -1;
    }
    
    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return -1;
    }
    
    // Load devices
    g_device_count = 0;
    int array_size = cJSON_GetArraySize(root);
    
    for (int i = 0; i < array_size && g_device_count < MAX_MATTER_DEVICES; i++) {
        cJSON *dev_obj = cJSON_GetArrayItem(root, i);
        
        if (dev_obj) {
            matter_device_t *dev = &g_devices[g_device_count];
            
            // Reconstruct 64-bit node ID
            uint32_t hi = cJSON_GetObjectItem(dev_obj, "node_id_hi")->valueint;
            uint32_t lo = cJSON_GetObjectItem(dev_obj, "node_id_lo")->valueint;
            dev->node_id = ((uint64_t)hi << 32) | lo;
            
            dev->endpoint_id = cJSON_GetObjectItem(dev_obj, "endpoint_id")->valueint;
            dev->device_type = cJSON_GetObjectItem(dev_obj, "device_type")->valueint;
            
            cJSON *name_obj = cJSON_GetObjectItem(dev_obj, "name");
            if (name_obj && cJSON_IsString(name_obj)) {
                strncpy(dev->name, name_obj->valuestring, sizeof(dev->name) - 1);
            }
            
            dev->virtual_zone_id = cJSON_GetObjectItem(dev_obj, "virtual_zone_id")->valueint;
            dev->virtual_relay_id = cJSON_GetObjectItem(dev_obj, "virtual_relay_id")->valueint;
            dev->is_commissioned = cJSON_GetObjectItem(dev_obj, "is_commissioned")->valueint;
            dev->is_online = false;  // Will be updated by connection status
            
            g_device_count++;
            
            ESP_LOGI(TAG, "Loaded device: %s (Node 0x%llX)", dev->name, dev->node_id);
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Loaded %d Matter devices", g_device_count);
    
    return 0;
}

/**
 * @brief Clear all Matter configuration from NVS
 */
int matter_clear_config(void) {
    ESP_LOGI(TAG, "Clearing Matter configuration...");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return -1;
    }
    
    err = nvs_erase_key(nvs_handle, NVS_KEY_DEVICES);
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to clear configuration: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Matter configuration cleared");
    
    return 0;
}
