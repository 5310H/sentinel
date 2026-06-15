/**
 * @file matter_controller_stub.c
 * @brief Enhanced Matter controller stub implementation
 * 
 * This is an enhanced stub for Matter protocol support that provides:
 * - Simulated device tracking and status
 * - Comprehensive logging and diagnostics
 * - API compatibility with full implementation
 * - Minimal memory footprint (~2KB)
 * 
 * Full Matter integration requires the complete CHIP SDK which adds:
 * - Several GB of build dependencies (gn, ninja, pigweed, zap tools)
 * - Significant build complexity and time (15-30 min builds)
 * - Large ROM/RAM footprint (~1-1.5MB additional)
 * 
 * For smart home integration, Sentinel-ESP includes:
 * - ESPHome Native API (fully functional, recommended)
 * - Tuya IoT integration
 * - MQTT support
 * 
 * To enable full Matter support:
 * 1. Set USE_MATTER_FULL=ON in CMakeLists.txt
 * 2. Source ~/esp/esp-matter/export.sh before building
 * 3. Use Matter SDK build scripts instead of idf.py
 * 
 * See MATTER_IMPLEMENTATION_COMPLETE.md for detailed steps.
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MATTER_STUB";

// Simulated device registry for stub mode
#define MAX_STUB_DEVICES 16

typedef struct {
    char device_id[32];
    char device_type[32];
    char device_name[64];
    bool is_active;
    uint32_t add_timestamp;
} stub_device_t;

static stub_device_t stub_devices[MAX_STUB_DEVICES] = {0};
static int stub_device_count = 0;
static bool stub_initialized = false;
static uint32_t init_timestamp = 0;

/**
 * Initialize Matter controller stub
 */
int matter_controller_init(void) {
    if (stub_initialized) {
        ESP_LOGW(TAG, "Matter stub already initialized");
        return 0;
    }

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         Matter Controller - Enhanced Stub Mode           ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Status: Stub mode active (simulation only)");
    ESP_LOGI(TAG, "Recommendation: Use ESPHome Native API for smart home");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Stub Features:");
    ESP_LOGI(TAG, "  • Device registry simulation (max %d devices)", MAX_STUB_DEVICES);
    ESP_LOGI(TAG, "  • Status reporting and diagnostics");
    ESP_LOGI(TAG, "  • API compatibility layer");
    ESP_LOGI(TAG, "  • Memory footprint: ~2KB");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Full Matter: Set USE_MATTER_FULL=ON in CMakeLists.txt");
    ESP_LOGI(TAG, "See: MATTER_IMPLEMENTATION_COMPLETE.md");
    
    memset(stub_devices, 0, sizeof(stub_devices));
    stub_device_count = 0;
    stub_initialized = true;
    init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    return 0;
}

/**
 * Deinitialize Matter controller stub
 */
int matter_controller_deinit(void) {
    if (!stub_initialized) {
        return 0;
    }
    
    ESP_LOGI(TAG, "Deinitializing Matter stub (removed %d simulated devices)", stub_device_count);
    
    memset(stub_devices, 0, sizeof(stub_devices));
    stub_device_count = 0;
    stub_initialized = false;
    
    return 0;
}

/**
 * Add a simulated Matter device
 */
int matter_device_add(const char *device_id, const char *device_type) {
    if (!stub_initialized) {
        ESP_LOGE(TAG, "Matter stub not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!device_id || !device_type) {
        ESP_LOGE(TAG, "Invalid device parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (stub_device_count >= MAX_STUB_DEVICES) {
        ESP_LOGE(TAG, "Stub device registry full (%d/%d)", stub_device_count, MAX_STUB_DEVICES);
        return ESP_ERR_NO_MEM;
    }
    
    // Check if device already exists
    for (int i = 0; i < MAX_STUB_DEVICES; i++) {
        if (stub_devices[i].is_active && strcmp(stub_devices[i].device_id, device_id) == 0) {
            ESP_LOGW(TAG, "Device already exists: %s", device_id);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Find free slot
    for (int i = 0; i < MAX_STUB_DEVICES; i++) {
        if (!stub_devices[i].is_active) {
            strncpy(stub_devices[i].device_id, device_id, sizeof(stub_devices[i].device_id) - 1);
            strncpy(stub_devices[i].device_type, device_type, sizeof(stub_devices[i].device_type) - 1);
            snprintf(stub_devices[i].device_name, sizeof(stub_devices[i].device_name), 
                    "Simulated %s", device_type);
            stub_devices[i].is_active = true;
            stub_devices[i].add_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
            stub_device_count++;
            
            ESP_LOGI(TAG, "✓ Simulated device added: '%s' (type: %s) [%d/%d]", 
                    device_id, device_type, stub_device_count, MAX_STUB_DEVICES);
            return 0;
        }
    }
    
    return ESP_FAIL;
}

/**
 * Remove a simulated Matter device
 */
int matter_device_remove(const char *device_id) {
    if (!stub_initialized) {
        ESP_LOGE(TAG, "Matter stub not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!device_id) {
        ESP_LOGE(TAG, "Invalid device ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < MAX_STUB_DEVICES; i++) {
        if (stub_devices[i].is_active && strcmp(stub_devices[i].device_id, device_id) == 0) {
            ESP_LOGI(TAG, "✓ Removed simulated device: '%s'", device_id);
            memset(&stub_devices[i], 0, sizeof(stub_device_t));
            stub_device_count--;
            return 0;
        }
    }
    
    ESP_LOGW(TAG, "Device not found: %s", device_id);
    return ESP_ERR_NOT_FOUND;
}

/**
 * Control a simulated Matter device
 */
int matter_device_control(const char *device_id, const char *command, const char *value) {
    if (!stub_initialized) {
        ESP_LOGE(TAG, "Matter stub not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!device_id || !command) {
        ESP_LOGE(TAG, "Invalid control parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device
    for (int i = 0; i < MAX_STUB_DEVICES; i++) {
        if (stub_devices[i].is_active && strcmp(stub_devices[i].device_id, device_id) == 0) {
            ESP_LOGI(TAG, "⚡ Simulated control: device='%s', cmd='%s', value='%s'", 
                    device_id, command, value ? value : "none");
            ESP_LOGD(TAG, "   (No actual device control - stub mode)");
            return 0;
        }
    }
    
    ESP_LOGW(TAG, "Device not found for control: %s", device_id);
    return ESP_ERR_NOT_FOUND;
}

/**
 * Simulate Matter device commissioning
 */
int matter_commission_device(const char *pairing_code, const char *device_name) {
    if (!stub_initialized) {
        ESP_LOGE(TAG, "Matter stub not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!pairing_code) {
        ESP_LOGE(TAG, "Invalid pairing code");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           Simulated Device Commissioning                ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Pairing Code: %s", pairing_code);
    if (device_name) {
        ESP_LOGI(TAG, "Device Name:  %s", device_name);
    }
    ESP_LOGI(TAG, "");
    ESP_LOGW(TAG, "⚠ This is a SIMULATION - no actual Matter commissioning");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "For real Matter device commissioning:");
    ESP_LOGI(TAG, "  1. Set USE_MATTER_FULL=ON in CMakeLists.txt");
    ESP_LOGI(TAG, "  2. Rebuild with Matter SDK environment");
    ESP_LOGI(TAG, "  3. Use Google Home/Apple Home apps");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Alternative: Use ESPHome Native API");
    ESP_LOGI(TAG, "  • Add ESPHome devices to Home Assistant");
    ESP_LOGI(TAG, "  • Use /api/esphome/devices endpoint");
    
    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * Get Matter controller status
 */
int matter_get_status(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!stub_initialized) {
        snprintf(buffer, buffer_size, 
                "{"
                "\"matter_enabled\":false,"
                "\"mode\":\"stub\","
                "\"status\":\"not_initialized\","
                "\"alternative\":\"esphome_api\""
                "}");
        return 0;
    }
    
    uint32_t uptime = (xTaskGetTickCount() * portTICK_PERIOD_MS) - init_timestamp;
    uint32_t free_heap = esp_get_free_heap_size();
    
    // Build device list JSON
    char device_list[512] = "[";
    int devices_added = 0;
    
    for (int i = 0; i < MAX_STUB_DEVICES && devices_added < stub_device_count; i++) {
        if (stub_devices[i].is_active) {
            char dev_json[256];  // Increased buffer size to accommodate max field lengths
            uint32_t dev_uptime = (xTaskGetTickCount() * portTICK_PERIOD_MS) - stub_devices[i].add_timestamp;
            
            snprintf(dev_json, sizeof(dev_json),
                    "%s{\"id\":\"%.31s\",\"type\":\"%.31s\",\"name\":\"%.63s\",\"uptime_ms\":%lu}",
                    devices_added > 0 ? "," : "",
                    stub_devices[i].device_id,
                    stub_devices[i].device_type,
                    stub_devices[i].device_name,
                    dev_uptime);
            
            if (strlen(device_list) + strlen(dev_json) < sizeof(device_list) - 2) {
                strcat(device_list, dev_json);
                devices_added++;
            }
        }
    }
    strcat(device_list, "]");
    
    snprintf(buffer, buffer_size,
            "{"
            "\"matter_enabled\":true,"
            "\"mode\":\"stub\","
            "\"status\":\"simulation_active\","
            "\"initialized\":true,"
            "\"uptime_ms\":%lu,"
            "\"devices\":{"
                "\"count\":%d,"
                "\"max\":%d,"
                "\"list\":%s"
            "},"
            "\"memory\":{"
                "\"free_heap\":%lu,"
                "\"stub_size\":2048"
            "},"
            "\"recommendations\":{"
                "\"primary\":\"esphome_api\","
                "\"full_matter\":\"Set USE_MATTER_FULL=ON\","
                "\"docs\":\"MATTER_IMPLEMENTATION_COMPLETE.md\""
            "}"
            "}",
            uptime,
            stub_device_count,
            MAX_STUB_DEVICES,
            device_list,
            free_heap);
    
    return 0;
}
