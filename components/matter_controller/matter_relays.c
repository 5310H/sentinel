/**
 * @file matter_relays.c
 * @brief Virtual relay integration for Matter output devices
 * 
 * Maps Matter switches/lights (8-31) to Sentinel virtual relays.
 * Integrates with HAL layer for unified relay control.
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "MATTER_RELAYS";

// Virtual relay mappings (relays 8-31)
#define VIRTUAL_RELAY_START 8
#define VIRTUAL_RELAY_END 31
#define MAX_VIRTUAL_RELAYS (VIRTUAL_RELAY_END - VIRTUAL_RELAY_START + 1)

typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    matter_device_type_t device_type;
    bool current_state;
    uint8_t brightness;      // For dimmable lights (0-254)
    bool is_active;
} virtual_relay_t;

static virtual_relay_t g_virtual_relays[MAX_VIRTUAL_RELAYS];
static SemaphoreHandle_t g_relay_mutex = NULL;

// Forward declarations
extern int matter_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on);
extern int matter_send_brightness(uint64_t node_id, uint16_t endpoint_id, uint8_t level);
extern int matter_send_lock_unlock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code);

/**
 * @brief Initialize virtual relay subsystem
 */
int matter_relays_init(void) {
    if (g_relay_mutex == NULL) {
        g_relay_mutex = xSemaphoreCreateMutex();
        if (g_relay_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create relay mutex");
            return -1;
        }
    }
    
    // Clear all virtual relays
    memset(g_virtual_relays, 0, sizeof(g_virtual_relays));
    
    ESP_LOGI(TAG, "Virtual relays initialized (8-31)");
    return 0;
}

/**
 * @brief Register Matter device as virtual relay
 */
int matter_register_virtual_relay(uint64_t node_id, uint16_t endpoint_id, 
                                   int relay_id, matter_device_type_t device_type) {
    if (relay_id < VIRTUAL_RELAY_START || relay_id > VIRTUAL_RELAY_END) {
        ESP_LOGE(TAG, "Invalid virtual relay ID: %d", relay_id);
        return -1;
    }
    
    int idx = relay_id - VIRTUAL_RELAY_START;
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    virtual_relay_t *vr = &g_virtual_relays[idx];
    vr->node_id = node_id;
    vr->endpoint_id = endpoint_id;
    vr->device_type = device_type;
    vr->current_state = false;
    vr->brightness = 0;
    vr->is_active = true;
    
    xSemaphoreGive(g_relay_mutex);
    
    ESP_LOGI(TAG, "Registered virtual relay %d: Node 0x%llX endpoint %d (type %d)", 
             relay_id, node_id, endpoint_id, device_type);
    
    return 0;
}

/**
 * @brief Unregister virtual relay
 */
int matter_unregister_virtual_relay(int relay_id) {
    if (relay_id < VIRTUAL_RELAY_START || relay_id > VIRTUAL_RELAY_END) {
        return -1;
    }
    
    int idx = relay_id - VIRTUAL_RELAY_START;
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    g_virtual_relays[idx].is_active = false;
    
    xSemaphoreGive(g_relay_mutex);
    
    ESP_LOGI(TAG, "Unregistered virtual relay %d", relay_id);
    
    return 0;
}

/**
 * @brief Control virtual relay (called by HAL wrapper)
 */
int matter_set_virtual_relay(int relay_id, bool state) {
    if (relay_id < VIRTUAL_RELAY_START || relay_id > VIRTUAL_RELAY_END) {
        return -1;
    }
    
    int idx = relay_id - VIRTUAL_RELAY_START;
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    virtual_relay_t *vr = &g_virtual_relays[idx];
    
    if (!vr->is_active) {
        xSemaphoreGive(g_relay_mutex);
        ESP_LOGW(TAG, "Virtual relay %d not active", relay_id);
        return -1;
    }
    
    uint64_t node_id = vr->node_id;
    uint16_t endpoint_id = vr->endpoint_id;
    matter_device_type_t device_type = vr->device_type;
    
    xSemaphoreGive(g_relay_mutex);
    
    // Send Matter command based on device type
    int result = -1;
    
    switch (device_type) {
        case MATTER_DEVICE_ON_OFF_SWITCH:
        case MATTER_DEVICE_DIMMABLE_LIGHT:
        case MATTER_DEVICE_COLOR_LIGHT:
            result = matter_send_on_off(node_id, endpoint_id, state);
            break;
            
        case MATTER_DEVICE_DOOR_LOCK:
            result = matter_send_lock_unlock(node_id, endpoint_id, state, NULL);
            break;
            
        default:
            ESP_LOGW(TAG, "Unsupported device type: %d", device_type);
            return -1;
    }
    
    if (result == 0) {
        xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
        vr->current_state = state;
        xSemaphoreGive(g_relay_mutex);
        
        ESP_LOGI(TAG, "Virtual relay %d set to %s", relay_id, state ? "ON" : "OFF");
    }
    
    return result;
}

/**
 * @brief Set brightness of dimmable virtual relay
 */
int matter_set_virtual_relay_brightness(int relay_id, uint8_t brightness) {
    if (relay_id < VIRTUAL_RELAY_START || relay_id > VIRTUAL_RELAY_END) {
        return -1;
    }
    
    int idx = relay_id - VIRTUAL_RELAY_START;
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    virtual_relay_t *vr = &g_virtual_relays[idx];
    
    if (!vr->is_active) {
        xSemaphoreGive(g_relay_mutex);
        return -1;
    }
    
    if (vr->device_type != MATTER_DEVICE_DIMMABLE_LIGHT &&
        vr->device_type != MATTER_DEVICE_COLOR_LIGHT) {
        xSemaphoreGive(g_relay_mutex);
        ESP_LOGW(TAG, "Device is not dimmable");
        return -1;
    }
    
    uint64_t node_id = vr->node_id;
    uint16_t endpoint_id = vr->endpoint_id;
    
    xSemaphoreGive(g_relay_mutex);
    
    int result = matter_send_brightness(node_id, endpoint_id, brightness);
    
    if (result == 0) {
        xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
        vr->brightness = brightness;
        xSemaphoreGive(g_relay_mutex);
        
        ESP_LOGI(TAG, "Virtual relay %d brightness set to %d", relay_id, brightness);
    }
    
    return result;
}

/**
 * @brief Get current state of virtual relay
 */
int matter_get_virtual_relay_state(int relay_id, bool *state) {
    if (relay_id < VIRTUAL_RELAY_START || relay_id > VIRTUAL_RELAY_END || !state) {
        return -1;
    }
    
    int idx = relay_id - VIRTUAL_RELAY_START;
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    if (!g_virtual_relays[idx].is_active) {
        xSemaphoreGive(g_relay_mutex);
        return -1;
    }
    
    *state = g_virtual_relays[idx].current_state;
    
    xSemaphoreGive(g_relay_mutex);
    
    return 0;
}

/**
 * @brief Trigger all alarm response devices
 * 
 * Called when alarm is triggered to turn on all lights, activate sirens, etc.
 */
int matter_trigger_alarm_response(void) {
    ESP_LOGI(TAG, "Triggering Matter alarm response");
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    int activated = 0;
    
    for (int i = 0; i < MAX_VIRTUAL_RELAYS; i++) {
        if (g_virtual_relays[i].is_active) {
            // Turn on all lights at full brightness
            if (g_virtual_relays[i].device_type == MATTER_DEVICE_DIMMABLE_LIGHT ||
                g_virtual_relays[i].device_type == MATTER_DEVICE_COLOR_LIGHT) {
                
                uint64_t node_id = g_virtual_relays[i].node_id;
                uint16_t endpoint = g_virtual_relays[i].endpoint_id;
                
                // Release mutex during command
                xSemaphoreGive(g_relay_mutex);
                
                matter_send_on_off(node_id, endpoint, true);
                matter_send_brightness(node_id, endpoint, 254);  // Max brightness
                
                xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
                
                g_virtual_relays[i].current_state = true;
                g_virtual_relays[i].brightness = 254;
                activated++;
            }
        }
    }
    
    xSemaphoreGive(g_relay_mutex);
    
    ESP_LOGI(TAG, "Activated %d Matter devices for alarm response", activated);
    
    return activated;
}

/**
 * @brief Turn off all alarm response devices
 */
int matter_clear_alarm_response(void) {
    ESP_LOGI(TAG, "Clearing Matter alarm response");
    
    xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
    
    for (int i = 0; i < MAX_VIRTUAL_RELAYS; i++) {
        if (g_virtual_relays[i].is_active && g_virtual_relays[i].current_state) {
            uint64_t node_id = g_virtual_relays[i].node_id;
            uint16_t endpoint = g_virtual_relays[i].endpoint_id;
            
            xSemaphoreGive(g_relay_mutex);
            
            matter_send_on_off(node_id, endpoint, false);
            
            xSemaphoreTake(g_relay_mutex, portMAX_DELAY);
            
            g_virtual_relays[i].current_state = false;
        }
    }
    
    xSemaphoreGive(g_relay_mutex);
    
    return 0;
}
