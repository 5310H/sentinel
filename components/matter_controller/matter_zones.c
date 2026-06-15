/**
 * @file matter_zones.c
 * @brief Virtual zone integration for Matter sensors
 * 
 * Maps Matter sensors (motion, contact) to Sentinel virtual zones (33-96).
 * Integrates with existing monitor/engine subsystem.
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "MATTER_ZONES";

// Virtual zone state tracking (zones 33-96)
#define VIRTUAL_ZONE_START 33
#define VIRTUAL_ZONE_END 96
#define MAX_VIRTUAL_ZONES (VIRTUAL_ZONE_END - VIRTUAL_ZONE_START + 1)

typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    bool current_state;         // Current sensor state
    bool debounce_state[3];     // Last 3 readings for debounce
    int debounce_count;         // Debounce counter
    uint32_t last_update_ms;
    bool is_active;             // Zone is configured
} virtual_zone_t;

static virtual_zone_t g_virtual_zones[MAX_VIRTUAL_ZONES];
static SemaphoreHandle_t g_zone_mutex = NULL;

// Forward declarations
extern int engine_get_arm_state(void);
extern void trigger_alarm(int zone_id, const char *reason);
extern void monitor_process_event(int zone_index);

/**
 * @brief Initialize virtual zone subsystem
 */
int matter_zones_init(void) {
    if (g_zone_mutex == NULL) {
        g_zone_mutex = xSemaphoreCreateMutex();
        if (g_zone_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create zone mutex");
            return -1;
        }
    }
    
    // Clear all virtual zones
    memset(g_virtual_zones, 0, sizeof(g_virtual_zones));
    
    ESP_LOGI(TAG, "Virtual zones initialized (33-96)");
    return 0;
}

/**
 * @brief Register Matter sensor as virtual zone
 */
int matter_register_virtual_zone(uint64_t node_id, uint16_t endpoint_id, int zone_id) {
    if (zone_id < VIRTUAL_ZONE_START || zone_id > VIRTUAL_ZONE_END) {
        ESP_LOGE(TAG, "Invalid virtual zone ID: %d", zone_id);
        return -1;
    }
    
    int idx = zone_id - VIRTUAL_ZONE_START;
    
    xSemaphoreTake(g_zone_mutex, portMAX_DELAY);
    
    virtual_zone_t *vz = &g_virtual_zones[idx];
    vz->node_id = node_id;
    vz->endpoint_id = endpoint_id;
    vz->current_state = false;
    vz->debounce_count = 0;
    vz->is_active = true;
    memset(vz->debounce_state, 0, sizeof(vz->debounce_state));
    
    xSemaphoreGive(g_zone_mutex);
    
    ESP_LOGI(TAG, "Registered virtual zone %d: Node 0x%llX endpoint %d", zone_id, node_id, endpoint_id);
    
    return 0;
}

/**
 * @brief Unregister virtual zone
 */
int matter_unregister_virtual_zone(int zone_id) {
    if (zone_id < VIRTUAL_ZONE_START || zone_id > VIRTUAL_ZONE_END) {
        return -1;
    }
    
    int idx = zone_id - VIRTUAL_ZONE_START;
    
    xSemaphoreTake(g_zone_mutex, portMAX_DELAY);
    
    g_virtual_zones[idx].is_active = false;
    
    xSemaphoreGive(g_zone_mutex);
    
    ESP_LOGI(TAG, "Unregistered virtual zone %d", zone_id);
    
    return 0;
}

/**
 * @brief Update virtual zone state from Matter sensor
 * 
 * Called by Matter cluster callbacks when sensor state changes.
 * Applies debouncing before triggering alarm logic.
 */
int matter_update_virtual_zone(uint64_t node_id, uint16_t endpoint_id, bool state) {
    xSemaphoreTake(g_zone_mutex, portMAX_DELAY);
    
    // Find zone by node ID and endpoint
    int zone_id = -1;
    virtual_zone_t *vz = NULL;
    
    for (int i = 0; i < MAX_VIRTUAL_ZONES; i++) {
        if (g_virtual_zones[i].is_active &&
            g_virtual_zones[i].node_id == node_id &&
            g_virtual_zones[i].endpoint_id == endpoint_id) {
            vz = &g_virtual_zones[i];
            zone_id = VIRTUAL_ZONE_START + i;
            break;
        }
    }
    
    if (!vz) {
        xSemaphoreGive(g_zone_mutex);
        ESP_LOGW(TAG, "Virtual zone not found for node 0x%llX", node_id);
        return -1;
    }
    
    // Update debounce buffer
    vz->debounce_state[vz->debounce_count % 3] = state;
    vz->debounce_count++;
    
    // Check if we have 3 consecutive identical readings
    if (vz->debounce_count >= 3) {
        bool all_same = (vz->debounce_state[0] == vz->debounce_state[1] &&
                        vz->debounce_state[1] == vz->debounce_state[2]);
        
        if (all_same && vz->current_state != state) {
            // State confirmed after debounce
            vz->current_state = state;
            vz->last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            
            ESP_LOGI(TAG, "Virtual zone %d state changed: %s", zone_id, state ? "TRIGGERED" : "CLEAR");
            
            // Release mutex before calling engine (prevent deadlock)
            xSemaphoreGive(g_zone_mutex);
            
            // Trigger alarm if system is armed and sensor activated
            if (state && engine_get_arm_state() >= 2) {
                char reason[64];
                snprintf(reason, sizeof(reason), "Matter Zone %d Triggered", zone_id);
                trigger_alarm(zone_id, reason);
            }
            
            return 0;
        }
    }
    
    xSemaphoreGive(g_zone_mutex);
    
    return 0;
}

/**
 * @brief Get current state of virtual zone
 */
int matter_get_virtual_zone_state(int zone_id, bool *state) {
    if (zone_id < VIRTUAL_ZONE_START || zone_id > VIRTUAL_ZONE_END || !state) {
        return -1;
    }
    
    int idx = zone_id - VIRTUAL_ZONE_START;
    
    xSemaphoreTake(g_zone_mutex, portMAX_DELAY);
    
    if (!g_virtual_zones[idx].is_active) {
        xSemaphoreGive(g_zone_mutex);
        return -1;
    }
    
    *state = g_virtual_zones[idx].current_state;
    
    xSemaphoreGive(g_zone_mutex);
    
    return 0;
}

/**
 * @brief Get all active virtual zones
 */
int matter_get_active_virtual_zones(int *zone_ids, int max_zones, int *count) {
    if (!zone_ids || !count) {
        return -1;
    }
    
    xSemaphoreTake(g_zone_mutex, portMAX_DELAY);
    
    *count = 0;
    for (int i = 0; i < MAX_VIRTUAL_ZONES && *count < max_zones; i++) {
        if (g_virtual_zones[i].is_active) {
            zone_ids[*count] = VIRTUAL_ZONE_START + i;
            (*count)++;
        }
    }
    
    xSemaphoreGive(g_zone_mutex);
    
    return 0;
}

/**
 * @brief Matter sensor callback wrapper
 * 
 * Called by Matter cluster subscription when sensor changes
 */
void matter_zone_sensor_callback(uint64_t node_id, uint16_t endpoint, bool state) {
    ESP_LOGI(TAG, "Sensor callback: Node 0x%llX endpoint %d = %s", 
             node_id, endpoint, state ? "TRIGGERED" : "CLEAR");
    
    matter_update_virtual_zone(node_id, endpoint, state);
}
