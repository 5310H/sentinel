/**
 * @file scheduler.h
 * @brief Task scheduler with cron-like expressions and intervals
 * 
 * Features:
 * - Cron expressions (minute, hour, day, month, weekday)
 * - Interval tasks (every N seconds)
 * - One-shot tasks (run once at specific time)
 * - Priority levels
 * - Persistent schedules
 * - Timezone aware
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef ESP_PLATFORM
    #include "esp_err.h"
#else
    typedef int esp_err_t;
    #define ESP_OK 0
    #define ESP_FAIL -1
    #define ESP_ERR_INVALID_ARG -2
    #define ESP_ERR_NOT_FOUND -4
    #define ESP_ERR_NO_MEM -5
#endif

#define SCHEDULER_MAX_TASKS 64
#define SCHEDULER_NAME_LEN 32

/**
 * @brief Schedule types
 */
typedef enum {
    SCHEDULE_INTERVAL,      // Run every N seconds
    SCHEDULE_CRON,          // Cron expression (minute hour day month weekday)
    SCHEDULE_ONCE,          // Run once at specific time
    SCHEDULE_SUNRISE,       // At sunrise (+/- offset) - Future
    SCHEDULE_SUNSET         // At sunset (+/- offset) - Future
} schedule_type_t;

/**
 * @brief Task priority levels
 */
typedef enum {
    SCHEDULE_PRIORITY_LOW = 0,
    SCHEDULE_PRIORITY_NORMAL = 1,
    SCHEDULE_PRIORITY_HIGH = 2,
    SCHEDULE_PRIORITY_CRITICAL = 3
} schedule_priority_t;

/**
 * @brief Task execution callback
 * @param user_data User-provided data passed to callback
 */
typedef void (*schedule_callback_t)(void *user_data);

/**
 * @brief Cron expression structure
 * 
 * Format: "minute hour day month weekday"
 * Examples:
 *   "0 6 * * *"       = 6:00 AM daily
 *   "0 18 * * 1-5"    = 6:00 PM Monday-Friday
 *   "0 0 1 * *"       = Midnight on 1st of month
 *   "30 9 * * 0"      = 9:30 AM on Sundays
 * 
 * Special characters:
 *   asterisk = any value
 *   asterisk-slash-N = every N (e.g., every 5 minutes, every 2 hours)
 *   N-M = range (e.g., 1-5 = Monday to Friday)
 *   N,M = list (e.g., 1,3,5 = Mon, Wed, Fri)
 */
typedef struct {
    uint64_t minutes;       // Bitmask: 0-59
    uint32_t hours;         // Bitmask: 0-23
    uint32_t days;          // Bitmask: 1-31
    uint16_t months;        // Bitmask: 1-12
    uint8_t weekdays;       // Bitmask: 0-6 (0=Sunday)
} cron_expression_t;

/**
 * @brief Action types for scheduled tasks
 */
typedef enum {
    SCHEDULER_ACTION_NONE = 0,          // No action (callback only)
    SCHEDULER_ACTION_SCENE_RECALL,      // Recall Zigbee scene
    SCHEDULER_ACTION_GROUP_CONTROL,     // Control Zigbee group
    SCHEDULER_ACTION_RELAY_CONTROL,     // Control relay output
    SCHEDULER_ACTION_MQTT_PUBLISH,      // Publish MQTT message
    SCHEDULER_ACTION_HTTP_REQUEST       // Make HTTP request
} scheduler_action_type_t;

/**
 * @brief Action data for scheduled tasks
 */
typedef struct {
    scheduler_action_type_t type;
    
    union {
        // SCHEDULER_ACTION_SCENE_RECALL
        struct {
            uint8_t scene_id;
        } scene;
        
        // SCHEDULER_ACTION_GROUP_CONTROL
        struct {
            uint8_t group_id;
            bool on;
            uint8_t level;      // 0-255, 0 = no change
        } group;
        
        // SCHEDULER_ACTION_RELAY_CONTROL
        struct {
            uint8_t relay_id;
            bool on;
        } relay;
        
        // SCHEDULER_ACTION_MQTT_PUBLISH
        struct {
            char topic[64];
            char payload[128];
        } mqtt;
        
        // SCHEDULER_ACTION_HTTP_REQUEST
        struct {
            char url[128];
            char method[8];     // GET, POST, etc.
        } http;
    } params;
} scheduler_action_t;

/**
 * @brief Scheduled task
 */
typedef struct {
    uint8_t task_id;
    char name[SCHEDULER_NAME_LEN];
    schedule_type_t type;
    schedule_priority_t priority;
    bool enabled;
    
    // Schedule data
    union {
        uint32_t interval_seconds;      // For SCHEDULE_INTERVAL
        cron_expression_t cron;          // For SCHEDULE_CRON
        time_t once_time;                // For SCHEDULE_ONCE
        int32_t sun_offset_minutes;      // For SCHEDULE_SUNRISE/SUNSET
    } schedule;
    
    char cron_string[64];                // Original cron expression (for SCHEDULE_CRON persistence)
    
    // Callback
    schedule_callback_t callback;
    void *user_data;
    
    // Action (alternative to callback for serializable tasks)
    scheduler_action_t action;
    
    // Execution tracking
    time_t last_run;
    time_t next_run;
    uint32_t run_count;
    uint32_t error_count;
} scheduler_task_t;

/**
 * @brief Task execution history
 */
typedef struct {
    uint32_t total_runs;
    uint32_t total_errors;
    time_t last_success;
    time_t last_error;
    uint32_t avg_duration_ms;
} schedule_history_t;

// ============================================================================
// Initialization & Control
// ============================================================================

/**
 * @brief Set location for sunrise/sunset calculations
 * @param latitude Latitude in degrees (-90 to 90)
 * @param longitude Longitude in degrees (-180 to 180)
 * @param timezone_offset Timezone offset in hours from UTC
 */
void scheduler_set_location(float latitude, float longitude, int timezone_offset);

/**
 * @brief Calculate sunrise time for given date
 * @param date Date to calculate for (Unix timestamp)
 * @return Sunrise time as Unix timestamp, or 0 if not calculated
 */
time_t scheduler_calculate_sunrise(time_t date);

/**
 * @brief Calculate sunset time for given date
 * @param date Date to calculate for (Unix timestamp)
 * @return Sunset time as Unix timestamp, or 0 if not calculated
 */
time_t scheduler_calculate_sunset(time_t date);

/**
 * @brief Initialize scheduler
 * @return ESP_OK on success
 */
esp_err_t scheduler_init(void);

/**
 * @brief Deinitialize scheduler and stop all tasks
 * @return ESP_OK on success
 */
esp_err_t scheduler_deinit(void);

/**
 * @brief Start scheduler (begins task execution)
 * @return ESP_OK on success
 */
esp_err_t scheduler_start(void);

/**
 * @brief Stop scheduler (pauses all tasks)
 * @return ESP_OK on success
 */
esp_err_t scheduler_stop(void);

/**
 * @brief Check if scheduler is running
 * @return true if running
 */
bool scheduler_is_running(void);

// ============================================================================
// Task Management
// ============================================================================

/**
 * @brief Add interval-based task (every N seconds)
 * @param name Task name (max 31 chars)
 * @param interval_seconds Interval in seconds
 * @param callback Function to call
 * @param user_data User data passed to callback
 * @param priority Task priority
 * @param task_id Output: assigned task ID
 * @return ESP_OK on success
 */
esp_err_t scheduler_add_interval_task(
    const char *name,
    uint32_t interval_seconds,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
);

/**
 * @brief Add cron-based task
 * @param name Task name
 * @param cron_expression Cron string (e.g., "0 6 * * *")
 * @param callback Function to call
 * @param user_data User data passed to callback
 * @param priority Task priority
 * @param task_id Output: assigned task ID
 * @return ESP_OK on success
 */
esp_err_t scheduler_add_cron_task(
    const char *name,
    const char *cron_expression,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
);

/**
 * @brief Add one-shot task (run once at specific time)
 * @param name Task name
 * @param run_time Unix timestamp when to run
 * @param callback Function to call
 * @param user_data User data passed to callback
 * @param priority Task priority
 * @param task_id Output: assigned task ID
 * @return ESP_OK on success
 */
esp_err_t scheduler_add_once_task(
    const char *name,
    time_t run_time,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
);

/**
 * @brief Delete task
 * @param task_id Task ID to delete
 * @return ESP_OK on success
 */
esp_err_t scheduler_delete_task(uint8_t task_id);

/**
 * @brief Enable task
 * @param task_id Task ID to enable
 * @return ESP_OK on success
 */
esp_err_t scheduler_enable_task(uint8_t task_id);

/**
 * @brief Disable task
 * @param task_id Task ID to disable
 * @return ESP_OK on success
 */
esp_err_t scheduler_disable_task(uint8_t task_id);

/**
 * @brief Check if task is enabled
 * @param task_id Task ID to check
 * @return true if enabled
 */
bool scheduler_is_task_enabled(uint8_t task_id);

/**
 * @brief Get task by ID
 * @param task_id Task ID
 * @param task Output: task data
 * @return ESP_OK on success
 */
esp_err_t scheduler_get_task(uint8_t task_id, scheduler_task_t *task);

/**
 * @brief Set task action
 * @param task_id Task ID
 * @param action Action to set
 * @return ESP_OK on success
 */
esp_err_t scheduler_set_task_action(uint8_t task_id, const scheduler_action_t *action);

/**
 * @brief Get all tasks
 * @param tasks Output: array of tasks
 * @param count Output: number of tasks
 * @return ESP_OK on success
 */
esp_err_t scheduler_get_all_tasks(scheduler_task_t *tasks, int *count);

/**
 * @brief Get next run time for task
 * @param task_id Task ID
 * @return Unix timestamp of next run, or 0 if disabled/invalid
 */
time_t scheduler_get_next_run(uint8_t task_id);

/**
 * @brief Get task execution history
 * @param task_id Task ID
 * @param history Output: execution history
 * @return ESP_OK on success
 */
esp_err_t scheduler_get_task_history(uint8_t task_id, schedule_history_t *history);

/**
 * @brief Trigger task immediately (bypass schedule)
 * @param task_id Task ID to run now
 * @return ESP_OK on success
 */
esp_err_t scheduler_trigger_task(uint8_t task_id);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Parse cron expression string into structure
 * @param expression Cron string (e.g., "0 6 * * *")
 * @param cron Output: parsed cron structure
 * @return ESP_OK on success
 */
esp_err_t scheduler_parse_cron(const char *expression, cron_expression_t *cron);

/**
 * @brief Check if cron expression matches current time
 * @param cron Cron expression
 * @param t Time to check against
 * @return true if matches
 */
bool scheduler_cron_matches(const cron_expression_t *cron, const struct tm *t);

/**
 * @brief Calculate next run time for cron expression
 * @param cron Cron expression
 * @param from_time Start time
 * @return Unix timestamp of next match
 */
time_t scheduler_cron_next(const cron_expression_t *cron, time_t from_time);

// ============================================================================
// Persistence Functions
// ============================================================================

/**
 * @brief Save all tasks to persistent storage
 * @param filepath Path to save file (default: /spiffs/scheduler.json)
 * @return ESP_OK on success
 */
esp_err_t scheduler_save_tasks(const char *filepath);

/**
 * @brief Load tasks from persistent storage
 * @param filepath Path to load file (default: /spiffs/scheduler.json)
 * @return ESP_OK on success
 */
esp_err_t scheduler_load_tasks(const char *filepath);

#endif // SCHEDULER_H
