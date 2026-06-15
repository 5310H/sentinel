/**
 * @file zigbee_automations.c
 * @brief Zigbee automation rules engine implementation
 */

#include "zigbee_automations.h"
#include "zigbee_mgr.h"
#include "zigbee_groups.h"
#include "zigbee_scenes.h"
#include "storage_mgr.h"
#include "scheduler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ZIGBEE_AUTO";

// Automation registry
static struct {
    zigbee_automation_t automations[ZIGBEE_MAX_AUTOMATIONS];
    int count;
    bool initialized;
    uint8_t scheduler_task_id;
} s_registry = {0};

// Forward declarations
static bool check_conditions(const zigbee_automation_t *automation);
static esp_err_t execute_actions(const zigbee_automation_t *automation);
static void timer_callback(void *arg);

esp_err_t zigbee_automations_init(void) {
    if (s_registry.initialized) {
        return ESP_OK;
    }
    
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.initialized = true;
    
    // Add scheduler task for time-based triggers (check every 60 seconds)
    esp_err_t ret = scheduler_add_interval_task(
        "zigbee_auto",
        60,  // 60 seconds
        timer_callback,
        NULL,
        SCHEDULE_PRIORITY_NORMAL,
        &s_registry.scheduler_task_id
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add scheduler task: %d", ret);
        return ret;
    }
    
    if (zigbee_automations_load() != ESP_OK) {
        ESP_LOGW(TAG, "No saved automations found, starting fresh");
    }
    
    ESP_LOGI(TAG, "Initialized with %d automations", s_registry.count);
    return ESP_OK;
}

void zigbee_automations_deinit(void) {
    if (!s_registry.initialized) {
        return;
    }
    
    if (s_registry.scheduler_task_id) {
        scheduler_delete_task(s_registry.scheduler_task_id);
    }
    
    zigbee_automations_save();
    memset(&s_registry, 0, sizeof(s_registry));
}

esp_err_t zigbee_automations_create(const char *name, uint8_t *automation_id) {
    if (!s_registry.initialized || !name || !automation_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_registry.count >= ZIGBEE_MAX_AUTOMATIONS) {
        ESP_LOGE(TAG, "Maximum automations reached (%d)", ZIGBEE_MAX_AUTOMATIONS);
        return ESP_ERR_NO_MEM;
    }
    
    // Find free automation ID
    uint8_t new_id = 0;
    for (int i = 0; i < ZIGBEE_MAX_AUTOMATIONS; i++) {
        bool id_used = false;
        for (int j = 0; j < s_registry.count; j++) {
            if (s_registry.automations[j].automation_id == i) {
                id_used = true;
                break;
            }
        }
        if (!id_used) {
            new_id = i;
            break;
        }
    }
    
    zigbee_automation_t *automation = &s_registry.automations[s_registry.count];
    automation->automation_id = new_id;
    strncpy(automation->name, name, ZIGBEE_AUTOMATION_NAME_LEN - 1);
    automation->name[ZIGBEE_AUTOMATION_NAME_LEN - 1] = '\0';
    automation->enabled = true;
    automation->condition_count = 0;
    automation->action_count = 0;
    
    s_registry.count++;
    *automation_id = new_id;
    
    ESP_LOGI(TAG, "Created automation %d: %s", new_id, name);
    zigbee_automations_save();
    
    return ESP_OK;
}

esp_err_t zigbee_automations_delete(uint8_t automation_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int idx = -1;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.automations[i].automation_id == automation_id) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    for (int i = idx; i < s_registry.count - 1; i++) {
        s_registry.automations[i] = s_registry.automations[i + 1];
    }
    s_registry.count--;
    
    ESP_LOGI(TAG, "Deleted automation %d", automation_id);
    zigbee_automations_save();
    
    return ESP_OK;
}

esp_err_t zigbee_automations_set_trigger(uint8_t automation_id, const zigbee_trigger_t *trigger) {
    if (!s_registry.initialized || !trigger) {
        return ESP_ERR_INVALID_ARG;
    }
    
    zigbee_automation_t *automation = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.automations[i].automation_id == automation_id) {
            automation = &s_registry.automations[i];
            break;
        }
    }
    
    if (!automation) {
        return ESP_ERR_NOT_FOUND;
    }
    
    automation->trigger = *trigger;
    
    ESP_LOGI(TAG, "Set trigger for automation %d", automation_id);
    zigbee_automations_save();
    
    return ESP_OK;
}

esp_err_t zigbee_automations_add_condition(uint8_t automation_id, const zigbee_condition_t *condition) {
    if (!s_registry.initialized || !condition) {
        return ESP_ERR_INVALID_ARG;
    }
    
    zigbee_automation_t *automation = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.automations[i].automation_id == automation_id) {
            automation = &s_registry.automations[i];
            break;
        }
    }
    
    if (!automation) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (automation->condition_count >= ZIGBEE_MAX_CONDITIONS) {
        ESP_LOGE(TAG, "Automation %d condition limit reached", automation_id);
        return ESP_ERR_NO_MEM;
    }
    
    automation->conditions[automation->condition_count++] = *condition;
    
    ESP_LOGI(TAG, "Added condition to automation %d", automation_id);
    zigbee_automations_save();
    
    return ESP_OK;
}

esp_err_t zigbee_automations_add_action(uint8_t automation_id, const zigbee_action_t *action) {
    if (!s_registry.initialized || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    
    zigbee_automation_t *automation = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.automations[i].automation_id == automation_id) {
            automation = &s_registry.automations[i];
            break;
        }
    }
    
    if (!automation) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (automation->action_count >= ZIGBEE_MAX_ACTIONS) {
        ESP_LOGE(TAG, "Automation %d action limit reached", automation_id);
        return ESP_ERR_NO_MEM;
    }
    
    automation->actions[automation->action_count++] = *action;
    
    ESP_LOGI(TAG, "Added action to automation %d", automation_id);
    zigbee_automations_save();
    
    return ESP_OK;
}

esp_err_t zigbee_automations_execute(uint8_t automation_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const zigbee_automation_t *automation = zigbee_automations_get(automation_id);
    if (!automation || !automation->enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Executing automation %d: %s", automation_id, automation->name);
    
    // Check conditions
    if (!check_conditions(automation)) {
        ESP_LOGD(TAG, "Automation %d conditions not met", automation_id);
        return ESP_OK; // Not an error, just conditions not met
    }
    
    // Execute actions
    return execute_actions(automation);
}

void zigbee_automations_check_time_triggers(void) {
    if (!s_registry.initialized) {
        return;
    }
    
    // Get current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    for (int i = 0; i < s_registry.count; i++) {
        zigbee_automation_t *automation = &s_registry.automations[i];
        
        if (!automation->enabled) {
            continue;
        }
        
        // Check if this is a time-based trigger
        if (automation->trigger.type == ZIGBEE_TRIGGER_TIME) {
            uint8_t trigger_hour = automation->trigger.params.time.hour;
            uint8_t trigger_minute = automation->trigger.params.time.minute;
            uint8_t days_mask = automation->trigger.params.time.days_mask;
            
            // Check if current time matches trigger time
            if (timeinfo.tm_hour == trigger_hour && timeinfo.tm_min == trigger_minute) {
                // Check if current day matches
                uint8_t current_day_bit = 1 << timeinfo.tm_wday; // 0=Sun, 1=Mon, etc.
                if (days_mask & current_day_bit) {
                    ESP_LOGI(TAG, "Time trigger matched for automation %d", automation->automation_id);
                    zigbee_automations_execute(automation->automation_id);
                }
            }
        }
    }
}

void zigbee_automations_notify_device_state(uint16_t short_addr, uint8_t state_type, uint8_t state_value) {
    if (!s_registry.initialized) {
        return;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        zigbee_automation_t *automation = &s_registry.automations[i];
        
        if (!automation->enabled) {
            continue;
        }
        
        // Check if this is a device state trigger
        if (automation->trigger.type == ZIGBEE_TRIGGER_DEVICE_STATE) {
            if (automation->trigger.params.device.short_addr == short_addr &&
                automation->trigger.params.device.state_type == state_type &&
                automation->trigger.params.device.state_value == state_value) {
                ESP_LOGI(TAG, "Device state trigger matched for automation %d", automation->automation_id);
                zigbee_automations_execute(automation->automation_id);
            }
        }
    }
}

void zigbee_automations_notify_zone_state(int zone_id, bool alarm_state) {
    if (!s_registry.initialized) {
        return;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        zigbee_automation_t *automation = &s_registry.automations[i];
        
        if (!automation->enabled) {
            continue;
        }
        
        // Check if this is a zone state trigger
        if (automation->trigger.type == ZIGBEE_TRIGGER_ZONE_STATE) {
            if (automation->trigger.params.zone.zone_id == zone_id &&
                automation->trigger.params.zone.alarm_state == alarm_state) {
                ESP_LOGI(TAG, "Zone state trigger matched for automation %d", automation->automation_id);
                zigbee_automations_execute(automation->automation_id);
            }
        }
    }
}

const zigbee_automation_t* zigbee_automations_get(uint8_t automation_id) {
    if (!s_registry.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.automations[i].automation_id == automation_id) {
            return &s_registry.automations[i];
        }
    }
    
    return NULL;
}

esp_err_t zigbee_automations_get_all(zigbee_automation_t *automations, int *count) {
    if (!s_registry.initialized || !automations || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(automations, s_registry.automations, sizeof(zigbee_automation_t) * s_registry.count);
    *count = s_registry.count;
    
    return ESP_OK;
}

// Helper function: check if conditions are met
static bool check_conditions(const zigbee_automation_t *automation) {
    // No conditions = always true
    if (automation->condition_count == 0) {
        return true;
    }
    
    // Get current time for time-based conditions
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // All conditions must be true (AND logic)
    for (int i = 0; i < automation->condition_count; i++) {
        const zigbee_condition_t *cond = &automation->conditions[i];
        
        switch (cond->type) {
            case ZIGBEE_CONDITION_TIME:
                {
                    uint8_t start_hour = cond->params.time_range.start_hour;
                    uint8_t start_min = cond->params.time_range.start_minute;
                    uint8_t end_hour = cond->params.time_range.end_hour;
                    uint8_t end_min = cond->params.time_range.end_minute;
                    
                    int current_mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
                    int start_mins = start_hour * 60 + start_min;
                    int end_mins = end_hour * 60 + end_min;
                    
                    if (current_mins < start_mins || current_mins > end_mins) {
                        return false;
                    }
                }
                break;
                
            case ZIGBEE_CONDITION_DEVICE_STATE:
                {
                    const zigbee_device_t *dev = zigbee_mgr_get_device(cond->params.device.short_addr);
                    if (!dev) {
                        return false;
                    }
                    
                    // Check state based on type (0=on_off, 1=level, etc.)
                    uint8_t actual_value = 0;
                    switch (cond->params.device.state_type) {
                        case 0: actual_value = dev->current_on_off ? 1 : 0; break;
                        case 1: actual_value = (dev->current_level * 100) / 254; break;
                        case 2: actual_value = dev->current_contact ? 1 : 0; break;
                        case 3: actual_value = dev->current_occupancy ? 1 : 0; break;
                    }
                    
                    if (actual_value != cond->params.device.state_value) {
                        return false;
                    }
                }
                break;
                
            case ZIGBEE_CONDITION_ZONE_STATE:
                // Would need zone manager integration
                // For now, skip
                break;
                
            case ZIGBEE_CONDITION_BATTERY_LOW:
                // Check if any device has low battery
                // For now, skip
                break;
        }
    }
    
    return true;
}

// Helper function: execute actions
static esp_err_t execute_actions(const zigbee_automation_t *automation) {
    for (int i = 0; i < automation->action_count; i++) {
        const zigbee_action_t *action = &automation->actions[i];
        
        switch (action->type) {
            case ZIGBEE_ACTION_DEVICE_ON_OFF:
                zigbee_mgr_set_on_off(action->params.on_off.short_addr, action->params.on_off.on);
                break;
                
            case ZIGBEE_ACTION_DEVICE_LEVEL:
                zigbee_mgr_set_level(action->params.level.short_addr, action->params.level.level);
                break;
                
            case ZIGBEE_ACTION_DEVICE_COLOR:
                zigbee_mgr_set_color(action->params.color.short_addr, action->params.color.rgb);
                break;
                
            case ZIGBEE_ACTION_GROUP_CONTROL:
                zigbee_groups_set_on_off(action->params.group.group_id, action->params.group.on);
                break;
                
            case ZIGBEE_ACTION_SCENE_RECALL:
                zigbee_scenes_recall(action->params.scene.scene_id);
                break;
                
            case ZIGBEE_ACTION_DELAY:
                vTaskDelay(action->params.delay.delay_ms / portTICK_PERIOD_MS);
                break;
        }
    }
    
    return ESP_OK;
}

// Timer callback for time-based triggers
static void timer_callback(void *arg) {
    zigbee_automations_check_time_triggers();
}

esp_err_t zigbee_automations_save(void) {
    // Simplified save - storing automations is complex, so we'll implement basic version
    ESP_LOGI(TAG, "Automation save not yet implemented (TODO)");
    return ESP_OK;
}

esp_err_t zigbee_automations_load(void) {
    // Simplified load
    ESP_LOGI(TAG, "Automation load not yet implemented (TODO)");
    return ESP_OK;
}
