/**
 * @file zigbee_automations.h
 * @brief Zigbee automation rules engine
 * 
 * If-then conditions for automated control.
 * Example: "If motion detected AND time is after sunset, turn on lights"
 */

#ifndef ZIGBEE_AUTOMATIONS_H
#define ZIGBEE_AUTOMATIONS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ZIGBEE_MAX_AUTOMATIONS 32
#define ZIGBEE_MAX_CONDITIONS 4
#define ZIGBEE_MAX_ACTIONS 8
#define ZIGBEE_AUTOMATION_NAME_LEN 32

/**
 * Trigger types
 */
typedef enum {
    ZIGBEE_TRIGGER_TIME,            // Time-based (cron-like)
    ZIGBEE_TRIGGER_DEVICE_STATE,    // Device state change
    ZIGBEE_TRIGGER_ZONE_STATE,      // Zone alarm state
    ZIGBEE_TRIGGER_BUTTON,          // Button press
    ZIGBEE_TRIGGER_SCENE            // Scene activation
} zigbee_trigger_type_t;

/**
 * Condition types
 */
typedef enum {
    ZIGBEE_CONDITION_TIME,          // Time range
    ZIGBEE_CONDITION_DEVICE_STATE,  // Device state
    ZIGBEE_CONDITION_ZONE_STATE,    // Zone state
    ZIGBEE_CONDITION_BATTERY_LOW    // Battery level
} zigbee_condition_type_t;

/**
 * Action types
 */
typedef enum {
    ZIGBEE_ACTION_DEVICE_ON_OFF,    // Turn device on/off
    ZIGBEE_ACTION_DEVICE_LEVEL,     // Set device brightness
    ZIGBEE_ACTION_DEVICE_COLOR,     // Set device color
    ZIGBEE_ACTION_GROUP_CONTROL,    // Control group
    ZIGBEE_ACTION_SCENE_RECALL,     // Recall scene
    ZIGBEE_ACTION_DELAY             // Delay next action (ms)
} zigbee_action_type_t;

/**
 * Trigger definition
 */
typedef struct {
    zigbee_trigger_type_t type;
    union {
        struct {
            uint8_t hour;           // 0-23
            uint8_t minute;         // 0-59
            uint8_t days_mask;      // Bit mask: Mon=0x01, Tue=0x02, ..., Sun=0x40
        } time;
        struct {
            uint16_t short_addr;    // Device short address
            uint8_t state_type;     // 0=on_off, 1=level, 2=contact, 3=occupancy
            uint8_t state_value;    // State value to trigger on
        } device;
        struct {
            int zone_id;            // Zone ID (50-64)
            bool alarm_state;       // true=triggered, false=cleared
        } zone;
    } params;
} zigbee_trigger_t;

/**
 * Condition definition
 */
typedef struct {
    zigbee_condition_type_t type;
    union {
        struct {
            uint8_t start_hour;
            uint8_t start_minute;
            uint8_t end_hour;
            uint8_t end_minute;
        } time_range;
        struct {
            uint16_t short_addr;
            uint8_t state_type;
            uint8_t state_value;
        } device;
        struct {
            int zone_id;
            bool alarm_state;
        } zone;
    } params;
} zigbee_condition_t;

/**
 * Action definition
 */
typedef struct {
    zigbee_action_type_t type;
    union {
        struct {
            uint16_t short_addr;
            bool on;
        } on_off;
        struct {
            uint16_t short_addr;
            uint8_t level;
        } level;
        struct {
            uint16_t short_addr;
            uint32_t rgb;
        } color;
        struct {
            uint8_t group_id;
            bool on;
        } group;
        struct {
            uint8_t scene_id;
        } scene;
        struct {
            uint32_t delay_ms;
        } delay;
    } params;
} zigbee_action_t;

/**
 * Automation rule structure
 */
typedef struct {
    uint8_t automation_id;                          // Automation ID (0-31)
    char name[ZIGBEE_AUTOMATION_NAME_LEN];         // Friendly name
    bool enabled;                                   // Rule enabled/disabled
    
    zigbee_trigger_t trigger;                       // What starts the automation
    zigbee_condition_t conditions[ZIGBEE_MAX_CONDITIONS]; // All must be true
    uint8_t condition_count;
    zigbee_action_t actions[ZIGBEE_MAX_ACTIONS];   // Execute in sequence
    uint8_t action_count;
} zigbee_automation_t;

/**
 * @brief Initialize automation engine
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_init(void);

/**
 * @brief Deinitialize automation engine
 */
void zigbee_automations_deinit(void);

/**
 * @brief Create a new automation rule
 * @param name Rule name
 * @param automation_id Output: assigned automation ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_create(const char *name, uint8_t *automation_id);

/**
 * @brief Delete an automation rule
 * @param automation_id Automation ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_delete(uint8_t automation_id);

/**
 * @brief Set automation trigger
 * @param automation_id Automation ID
 * @param trigger Trigger definition
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_set_trigger(uint8_t automation_id, const zigbee_trigger_t *trigger);

/**
 * @brief Add condition to automation
 * @param automation_id Automation ID
 * @param condition Condition definition
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_add_condition(uint8_t automation_id, const zigbee_condition_t *condition);

/**
 * @brief Add action to automation
 * @param automation_id Automation ID
 * @param action Action definition
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_add_action(uint8_t automation_id, const zigbee_action_t *action);

/**
 * @brief Execute automation (manually)
 * @param automation_id Automation ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_execute(uint8_t automation_id);

/**
 * @brief Check and execute time-based automations
 * Called every minute by timer
 */
void zigbee_automations_check_time_triggers(void);

/**
 * @brief Notify automation engine of device state change
 * @param short_addr Device short address
 * @param state_type State type (0=on_off, 1=level, etc.)
 * @param state_value New state value
 */
void zigbee_automations_notify_device_state(uint16_t short_addr, uint8_t state_type, uint8_t state_value);

/**
 * @brief Notify automation engine of zone state change
 * @param zone_id Zone ID
 * @param alarm_state Alarm state
 */
void zigbee_automations_notify_zone_state(int zone_id, bool alarm_state);

/**
 * @brief Get automation info
 * @param automation_id Automation ID
 * @return Pointer to automation structure or NULL
 */
const zigbee_automation_t* zigbee_automations_get(uint8_t automation_id);

/**
 * @brief Get all automations
 * @param automations Output array
 * @param count Output: number of automations
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_get_all(zigbee_automation_t *automations, int *count);

/**
 * @brief Save automations to storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_save(void);

/**
 * @brief Load automations from storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_automations_load(void);

#endif // ZIGBEE_AUTOMATIONS_H
