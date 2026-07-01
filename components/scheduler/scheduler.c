/**
 * @file scheduler.c
 * @brief Task scheduler implementation
 */

#include "scheduler.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
    #include "esp_log.h"
    #include "esp_timer.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "mongoose.h"
    static const char *TAG = "SCHEDULER";
#else
    #include <pthread.h>
    #include <unistd.h>
    #define ESP_LOGI(tag, fmt, ...) printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, fmt, ...) printf("[%s] WARN: " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGE(tag, fmt, ...) printf("[%s] ERROR: " fmt "\n", tag, ##__VA_ARGS__)
#endif

// Location for sunrise/sunset calculations
typedef struct {
    float latitude;
    float longitude;
    int timezone_offset;
    bool configured;
} location_config_t;

static location_config_t s_location = {0};

// Scheduler state
typedef struct {
    scheduler_task_t tasks[SCHEDULER_MAX_TASKS];
    int task_count;
    bool running;
    bool initialized;
    
#ifdef ESP_PLATFORM
    esp_timer_handle_t timer;
#else
    pthread_t thread;
    bool thread_running;
#endif
} scheduler_state_t;

static scheduler_state_t s_scheduler = {0};

// Forward declarations
static void scheduler_tick(void *arg);
static void execute_task(scheduler_task_t *task);
static time_t calculate_next_run(scheduler_task_t *task);

// External function declarations (weak symbols - optional dependencies)
extern void mqtt_publish_alert(const char *topic, const char *msg) __attribute__((weak));
extern struct mg_mgr *get_mongoose_mgr(void) __attribute__((weak));

// ============================================================================
// Sunrise/Sunset Calculations
// ============================================================================

#define PI 3.14159265358979323846
#define DEG_TO_RAD (PI / 180.0)
#define RAD_TO_DEG (180.0 / PI)

void scheduler_set_location(float latitude, float longitude, int timezone_offset) {
    s_location.latitude = latitude;
    s_location.longitude = longitude;
    s_location.timezone_offset = timezone_offset;
    s_location.configured = true;
    ESP_LOGI(TAG, "Location set: lat=%.2f, lon=%.2f, tz=%d", latitude, longitude, timezone_offset);
}

// Calculate sunrise/sunset time using simplified algorithm
static time_t calculate_sun_time(time_t date, bool is_sunrise) {
    if (!s_location.configured) {
        ESP_LOGW(TAG, "Location not configured for sun calculations");
        return 0;
    }
    
    // Get day of year
    struct tm *t = gmtime(&date);
    int day_of_year = t->tm_yday + 1;
    
    // Solar calculations (simplified)
    float lat_rad = s_location.latitude * DEG_TO_RAD;
    float solar_declination = 23.45 * DEG_TO_RAD * sin(2.0 * PI * (284 + day_of_year) / 365.0);
    
    // Hour angle for sunrise/sunset (when sun is at horizon)
    float cos_hour_angle = -tan(lat_rad) * tan(solar_declination);
    
    // Check if sun rises/sets today (polar regions might not)
    if (cos_hour_angle > 1.0 || cos_hour_angle < -1.0) {
        ESP_LOGW(TAG, "Sun does not rise/set at this location on this day");
        return 0;
    }
    
    float hour_angle = acos(cos_hour_angle) * RAD_TO_DEG;
    
    // Calculate solar noon (local)
    float longitude_correction = s_location.longitude * 4.0; // 4 minutes per degree
    float equation_of_time = 0; // Simplified - should vary by day
    float solar_noon = 12.0 - (longitude_correction / 60.0) + (equation_of_time / 60.0);
    
    // Calculate sunrise/sunset
    float sun_time;
    if (is_sunrise) {
        sun_time = solar_noon - (hour_angle / 15.0); // 15 degrees per hour
    } else {
        sun_time = solar_noon + (hour_angle / 15.0);
    }
    
    // Convert to Unix timestamp
    struct tm result_tm = *t;
    result_tm.tm_hour = (int)sun_time;
    result_tm.tm_min = (int)((sun_time - result_tm.tm_hour) * 60.0);
    result_tm.tm_sec = 0;
    
    // Apply timezone offset
    time_t result = mktime(&result_tm) - (s_location.timezone_offset * 3600);
    
    return result;
}

time_t scheduler_calculate_sunrise(time_t date) {
    return calculate_sun_time(date, true);
}

time_t scheduler_calculate_sunset(time_t date) {
    return calculate_sun_time(date, false);
}

// ============================================================================
// Cron Parsing
// ============================================================================

static bool parse_cron_field(const char *field, uint64_t *bitmask, int min, int max) {
    *bitmask = 0;
    
    // Handle "*" (all values)
    if (strcmp(field, "*") == 0) {
        for (int i = min; i <= max; i++) {
            *bitmask |= (1ULL << i);
        }
        return true;
    }
    
    // Handle "*/N" (every N)
    if (strncmp(field, "*/", 2) == 0) {
        int step = atoi(field + 2);
        if (step <= 0 || step > max) return false;
        for (int i = min; i <= max; i += step) {
            *bitmask |= (1ULL << i);
        }
        return true;
    }
    
    // Handle ranges "N-M" and lists "N,M,..."
    char *field_copy = strdup(field);
    char *token = strtok(field_copy, ",");
    
    while (token) {
        char *dash = strchr(token, '-');
        if (dash) {
            // Range
            int start = atoi(token);
            int end = atoi(dash + 1);
            if (start < min || end > max || start > end) {
                free(field_copy);
                return false;
            }
            for (int i = start; i <= end; i++) {
                *bitmask |= (1ULL << i);
            }
        } else {
            // Single value
            int val = atoi(token);
            if (val < min || val > max) {
                free(field_copy);
                return false;
            }
            *bitmask |= (1ULL << val);
        }
        token = strtok(NULL, ",");
    }
    
    free(field_copy);
    return true;
}

esp_err_t scheduler_parse_cron(const char *expression, cron_expression_t *cron) {
    if (!expression || !cron) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(cron, 0, sizeof(cron_expression_t));
    
    // Parse "minute hour day month weekday"
    char expr_copy[128];
    strncpy(expr_copy, expression, sizeof(expr_copy) - 1);
    expr_copy[sizeof(expr_copy) - 1] = '\0';
    
    char *fields[5];
    int field_count = 0;
    
    char *token = strtok(expr_copy, " ");
    while (token && field_count < 5) {
        fields[field_count++] = token;
        token = strtok(NULL, " ");
    }
    
    if (field_count != 5) {
        ESP_LOGE(TAG, "Invalid cron expression: expected 5 fields, got %d", field_count);
        return ESP_FAIL;
    }
    
    // Parse each field
    if (!parse_cron_field(fields[0], &cron->minutes, 0, 59)) {
        ESP_LOGE(TAG, "Invalid minute field: %s", fields[0]);
        return ESP_FAIL;
    }
    
    uint64_t hours_64;
    if (!parse_cron_field(fields[1], &hours_64, 0, 23)) {
        ESP_LOGE(TAG, "Invalid hour field: %s", fields[1]);
        return ESP_FAIL;
    }
    cron->hours = (uint32_t)hours_64;
    
    uint64_t days_64;
    if (!parse_cron_field(fields[2], &days_64, 1, 31)) {
        ESP_LOGE(TAG, "Invalid day field: %s", fields[2]);
        return ESP_FAIL;
    }
    cron->days = (uint32_t)days_64;
    
    uint64_t months_64;
    if (!parse_cron_field(fields[3], &months_64, 1, 12)) {
        ESP_LOGE(TAG, "Invalid month field: %s", fields[3]);
        return ESP_FAIL;
    }
    cron->months = (uint16_t)months_64;
    
    uint64_t weekdays_64;
    if (!parse_cron_field(fields[4], &weekdays_64, 0, 6)) {
        ESP_LOGE(TAG, "Invalid weekday field: %s", fields[4]);
        return ESP_FAIL;
    }
    cron->weekdays = (uint8_t)weekdays_64;
    
    return ESP_OK;
}

bool scheduler_cron_matches(const cron_expression_t *cron, const struct tm *t) {
    if (!cron || !t) return false;
    
    // Check minute (0-59)
    if (!(cron->minutes & (1ULL << t->tm_min))) return false;
    
    // Check hour (0-23)
    if (!(cron->hours & (1U << t->tm_hour))) return false;
    
    // Check day of month (1-31)
    if (!(cron->days & (1U << t->tm_mday))) return false;
    
    // Check month (1-12)
    if (!(cron->months & (1U << (t->tm_mon + 1)))) return false;
    
    // Check weekday (0-6, 0=Sunday)
    if (!(cron->weekdays & (1U << t->tm_wday))) return false;
    
    return true;
}

time_t scheduler_cron_next(const cron_expression_t *cron, time_t from_time) {
    struct tm t;
    localtime_r(&from_time, &t);
    
    // Start from next minute
    t.tm_sec = 0;
    t.tm_min++;
    time_t check_time = mktime(&t);
    
    // Check up to 2 years in future (prevent infinite loop)
    time_t max_time = from_time + (365 * 2 * 24 * 60 * 60);
    
    while (check_time < max_time) {
        localtime_r(&check_time, &t);
        if (scheduler_cron_matches(cron, &t)) {
            return check_time;
        }
        // Jump to next minute
        check_time += 60;
    }
    
    return 0;  // No match found
}

// ============================================================================
// Task Management
// ============================================================================

static scheduler_task_t* find_task(uint8_t task_id) {
    for (int i = 0; i < s_scheduler.task_count; i++) {
        if (s_scheduler.tasks[i].task_id == task_id) {
            return &s_scheduler.tasks[i];
        }
    }
    return NULL;
}

static uint8_t allocate_task_id(void) {
    static uint8_t next_id = 1;
    return next_id++;
}

static time_t calculate_next_run(scheduler_task_t *task) {
    time_t now = time(NULL);
    
    switch (task->type) {
        case SCHEDULE_INTERVAL:
            if (task->last_run == 0) {
                return now;  // Run immediately first time
            }
            return task->last_run + task->schedule.interval_seconds;
            
        case SCHEDULE_CRON:
            return scheduler_cron_next(&task->schedule.cron, now);
            
        case SCHEDULE_ONCE:
            return task->schedule.once_time;
            
        case SCHEDULE_SUNRISE: {
            time_t sunrise = scheduler_calculate_sunrise(now);
            if (sunrise == 0) return 0;
            // If sunrise already passed today, calculate tomorrow
            if (sunrise <= now) {
                sunrise = scheduler_calculate_sunrise(now + 86400);
            }
            return sunrise;
        }
            
        case SCHEDULE_SUNSET: {
            time_t sunset = scheduler_calculate_sunset(now);
            if (sunset == 0) return 0;
            // If sunset already passed today, calculate tomorrow
            if (sunset <= now) {
                sunset = scheduler_calculate_sunset(now + 86400);
            }
            return sunset;
        }
            
        default:
            return 0;
    }
}

esp_err_t scheduler_add_interval_task(
    const char *name,
    uint32_t interval_seconds,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
) {
    if (!name || !callback || !task_id || interval_seconds == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_scheduler.task_count >= SCHEDULER_MAX_TASKS) {
        ESP_LOGE(TAG, "Task limit reached (%d)", SCHEDULER_MAX_TASKS);
        return ESP_ERR_NO_MEM;
    }
    
    scheduler_task_t *task = &s_scheduler.tasks[s_scheduler.task_count++];
    memset(task, 0, sizeof(scheduler_task_t));
    
    task->task_id = allocate_task_id();
    strncpy(task->name, name, SCHEDULER_NAME_LEN - 1);
    task->type = SCHEDULE_INTERVAL;
    task->priority = priority;
    task->enabled = true;
    task->schedule.interval_seconds = interval_seconds;
    task->callback = callback;
    task->user_data = user_data;
    task->next_run = calculate_next_run(task);
    
    *task_id = task->task_id;
    
    ESP_LOGI(TAG, "Added interval task '%s' (ID:%d, interval:%" PRIu32 "s)", name, task->task_id, interval_seconds);
    return ESP_OK;
}

esp_err_t scheduler_add_cron_task(
    const char *name,
    const char *cron_expression,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
) {
    if (!name || !cron_expression || !callback || !task_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_scheduler.task_count >= SCHEDULER_MAX_TASKS) {
        ESP_LOGE(TAG, "Task limit reached (%d)", SCHEDULER_MAX_TASKS);
        return ESP_ERR_NO_MEM;
    }
    
    cron_expression_t cron;
    if (scheduler_parse_cron(cron_expression, &cron) != ESP_OK) {
        return ESP_FAIL;
    }
    
    scheduler_task_t *task = &s_scheduler.tasks[s_scheduler.task_count++];
    memset(task, 0, sizeof(scheduler_task_t));
    
    task->task_id = allocate_task_id();
    strncpy(task->name, name, SCHEDULER_NAME_LEN - 1);
    task->type = SCHEDULE_CRON;
    task->priority = priority;
    task->enabled = true;
    task->schedule.cron = cron;
    strncpy(task->cron_string, cron_expression, sizeof(task->cron_string) - 1);
    task->callback = callback;
    task->user_data = user_data;
    task->next_run = calculate_next_run(task);
    
    *task_id = task->task_id;
    
    ESP_LOGI(TAG, "Added cron task '%s' (ID:%d, expr:%s)", name, task->task_id, cron_expression);
    return ESP_OK;
}

esp_err_t scheduler_add_once_task(
    const char *name,
    time_t run_time,
    schedule_callback_t callback,
    void *user_data,
    schedule_priority_t priority,
    uint8_t *task_id
) {
    if (!name || !callback || !task_id || run_time <= time(NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_scheduler.task_count >= SCHEDULER_MAX_TASKS) {
        ESP_LOGE(TAG, "Task limit reached (%d)", SCHEDULER_MAX_TASKS);
        return ESP_ERR_NO_MEM;
    }
    
    scheduler_task_t *task = &s_scheduler.tasks[s_scheduler.task_count++];
    memset(task, 0, sizeof(scheduler_task_t));
    
    task->task_id = allocate_task_id();
    strncpy(task->name, name, SCHEDULER_NAME_LEN - 1);
    task->type = SCHEDULE_ONCE;
    task->priority = priority;
    task->enabled = true;
    task->schedule.once_time = run_time;
    task->callback = callback;
    task->user_data = user_data;
    task->next_run = run_time;
    
    *task_id = task->task_id;
    
    ESP_LOGI(TAG, "Added one-shot task '%s' (ID:%d)", name, task->task_id);
    return ESP_OK;
}

esp_err_t scheduler_delete_task(uint8_t task_id) {
    for (int i = 0; i < s_scheduler.task_count; i++) {
        if (s_scheduler.tasks[i].task_id == task_id) {
            // Shift remaining tasks down
            memmove(&s_scheduler.tasks[i], &s_scheduler.tasks[i + 1],
                    (s_scheduler.task_count - i - 1) * sizeof(scheduler_task_t));
            s_scheduler.task_count--;
            ESP_LOGI(TAG, "Deleted task ID:%d", task_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t scheduler_enable_task(uint8_t task_id) {
    scheduler_task_t *task = find_task(task_id);
    if (!task) return ESP_ERR_NOT_FOUND;
    
    task->enabled = true;
    task->next_run = calculate_next_run(task);
    ESP_LOGI(TAG, "Enabled task '%s' (ID:%d)", task->name, task_id);
    return ESP_OK;
}

esp_err_t scheduler_disable_task(uint8_t task_id) {
    scheduler_task_t *task = find_task(task_id);
    if (!task) return ESP_ERR_NOT_FOUND;
    
    task->enabled = false;
    ESP_LOGI(TAG, "Disabled task '%s' (ID:%d)", task->name, task_id);
    return ESP_OK;
}

bool scheduler_is_task_enabled(uint8_t task_id) {
    scheduler_task_t *task = find_task(task_id);
    return task ? task->enabled : false;
}

esp_err_t scheduler_get_task(uint8_t task_id, scheduler_task_t *task) {
    if (!task) return ESP_ERR_INVALID_ARG;
    
    scheduler_task_t *found = find_task(task_id);
    if (!found) return ESP_ERR_NOT_FOUND;
    
    memcpy(task, found, sizeof(scheduler_task_t));
    return ESP_OK;
}

esp_err_t scheduler_set_task_action(uint8_t task_id, const scheduler_action_t *action) {
    if (!action) return ESP_ERR_INVALID_ARG;
    
    scheduler_task_t *task = find_task(task_id);
    if (!task) return ESP_ERR_NOT_FOUND;
    
    memcpy(&task->action, action, sizeof(scheduler_action_t));
    return ESP_OK;
}

esp_err_t scheduler_get_all_tasks(scheduler_task_t *tasks, int *count) {
    if (!tasks || !count) return ESP_ERR_INVALID_ARG;
    
    memcpy(tasks, s_scheduler.tasks, s_scheduler.task_count * sizeof(scheduler_task_t));
    *count = s_scheduler.task_count;
    return ESP_OK;
}

time_t scheduler_get_next_run(uint8_t task_id) {
    scheduler_task_t *task = find_task(task_id);
    return task ? task->next_run : 0;
}

esp_err_t scheduler_trigger_task(uint8_t task_id) {
    scheduler_task_t *task = find_task(task_id);
    if (!task) return ESP_ERR_NOT_FOUND;
    
    if (!task->enabled) {
        ESP_LOGW(TAG, "Task '%s' is disabled, cannot trigger", task->name);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Manually triggering task '%s'", task->name);
    execute_task(task);
    return ESP_OK;
}

// ============================================================================
// Execution
// ============================================================================

static esp_err_t execute_action(const scheduler_action_t *action) {
    if (!action || action->type == SCHEDULER_ACTION_NONE) {
        return ESP_OK;  // No action to execute
    }
    
    ESP_LOGI(TAG, "Executing action type %d", action->type);
    
    switch (action->type) {
        case SCHEDULER_ACTION_SCENE_RECALL:
            // Zigbee disabled to save DRAM
            #if 0
            #ifdef ESP_PLATFORM
            // Call zigbee scene recall
            extern esp_err_t zigbee_scenes_recall(uint8_t scene_id);
            return zigbee_scenes_recall(action->params.scene.scene_id);
            #else
            ESP_LOGI(TAG, "Would recall scene %d", action->params.scene.scene_id);
            return ESP_OK;
            #endif
            #else
            ESP_LOGI(TAG, "Zigbee scene recall disabled");
            return ESP_ERR_NOT_SUPPORTED;
            #endif
            
        case SCHEDULER_ACTION_GROUP_CONTROL:
            // Zigbee disabled to save DRAM
            #if 0
            #ifdef ESP_PLATFORM
            // Call zigbee group control
            extern esp_err_t zigbee_groups_set_on_off(uint8_t group_id, bool on);
            extern esp_err_t zigbee_groups_set_level(uint8_t group_id, uint8_t level);
            esp_err_t result = zigbee_groups_set_on_off(action->params.group.group_id, 
                                                        action->params.group.on);
            if (result == ESP_OK && action->params.group.level > 0) {
                result = zigbee_groups_set_level(action->params.group.group_id, 
                                                 action->params.group.level);
            }
            return result;
            #else
            ESP_LOGI(TAG, "Would control group %d: on=%d level=%d", 
                     action->params.group.group_id, 
                     action->params.group.on,
                     action->params.group.level);
            return ESP_OK;
            #endif
            #else
            ESP_LOGI(TAG, "Zigbee group control disabled");
            return ESP_ERR_NOT_SUPPORTED;
            #endif
            
        case SCHEDULER_ACTION_RELAY_CONTROL:
            #ifdef ESP_PLATFORM
            // Call HAL relay control
            extern void hal_set_relay(int relay_id, bool state);
            hal_set_relay(action->params.relay.relay_id, action->params.relay.on);
            return ESP_OK;
            #else
            ESP_LOGI(TAG, "Would control relay %d: on=%d", 
                     action->params.relay.relay_id, 
                     action->params.relay.on);
            return ESP_OK;
            #endif
            
        case SCHEDULER_ACTION_MQTT_PUBLISH:
            #ifdef ESP_PLATFORM
            // Call MQTT publish from mqtt_app component (if available)
            if (mqtt_publish_alert) {
                mqtt_publish_alert(action->params.mqtt.topic, action->params.mqtt.payload);
                ESP_LOGI(TAG, "Published MQTT to %s: %s", 
                         action->params.mqtt.topic, 
                         action->params.mqtt.payload);
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "MQTT not available - would publish to %s: %s",
                         action->params.mqtt.topic,
                         action->params.mqtt.payload);
                return ESP_FAIL;
            }
            #else
            ESP_LOGI(TAG, "Would publish MQTT to %s: %s", 
                     action->params.mqtt.topic, 
                     action->params.mqtt.payload);
            return ESP_OK;
            #endif
            
        case SCHEDULER_ACTION_HTTP_REQUEST:
            #ifdef ESP_PLATFORM
            // Use mongoose to make HTTP request (if available)
            if (get_mongoose_mgr) {
                struct mg_mgr *mgr = get_mongoose_mgr();
                if (mgr) {
                    // Make simple HTTP GET request (POST/PUT would need body handling)
                    mg_http_connect(mgr, action->params.http.url, NULL, NULL);
                    ESP_LOGI(TAG, "Initiated HTTP %s to %s", 
                             action->params.http.method, 
                             action->params.http.url);
                    return ESP_OK;
                } else {
                    ESP_LOGW(TAG, "Mongoose manager not available for HTTP request");
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGW(TAG, "Mongoose not available - would make HTTP %s to %s",
                         action->params.http.method,
                         action->params.http.url);
                return ESP_FAIL;
            }
            #else
            ESP_LOGI(TAG, "Would make HTTP %s to %s", 
                     action->params.http.method, 
                     action->params.http.url);
            return ESP_OK;
            #endif
            
        default:
            ESP_LOGW(TAG, "Unknown action type: %d", action->type);
            return ESP_FAIL;
    }
}

static void execute_task(scheduler_task_t *task) {
    if (!task) return;
    
    time_t start = time(NULL);
    
    ESP_LOGI(TAG, "Executing task '%s' (ID:%d, priority:%d)", 
             task->name, task->task_id, task->priority);
    
    // Execute callback if present
    if (task->callback) {
        task->callback(task->user_data);
    }
    
    // Execute action if present
    if (task->action.type != SCHEDULER_ACTION_NONE) {
        esp_err_t result = execute_action(&task->action);
        if (result != ESP_OK) {
            task->error_count++;
            ESP_LOGE(TAG, "Task '%s' action failed: %d", task->name, result);
        }
    }
    
    // Update stats
    task->last_run = start;
    task->run_count++;
    
    // Calculate next run
    if (task->type == SCHEDULE_ONCE) {
        // One-shot tasks auto-disable after running
        task->enabled = false;
        ESP_LOGI(TAG, "One-shot task '%s' completed and disabled", task->name);
    } else {
        task->next_run = calculate_next_run(task);
    }
}

static void scheduler_tick(void *arg) {
    if (!s_scheduler.running) return;
    
    time_t now = time(NULL);
    
    // Check each task
    for (int i = 0; i < s_scheduler.task_count; i++) {
        scheduler_task_t *task = &s_scheduler.tasks[i];
        
        if (!task->enabled) continue;
        if (task->next_run == 0) continue;
        if (task->next_run > now) continue;
        
        // Time to execute
        execute_task(task);
    }
}

// ============================================================================
// Initialization
// ============================================================================

#ifdef ESP_PLATFORM
static void timer_callback(void *arg) {
    scheduler_tick(arg);
}
#else
static void* thread_func(void *arg) {
    while (s_scheduler.thread_running) {
        scheduler_tick(arg);
        sleep(1);  // Check every second
    }
    return NULL;
}
#endif

esp_err_t scheduler_init(void) {
    if (s_scheduler.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    memset(&s_scheduler, 0, sizeof(scheduler_state_t));
    s_scheduler.initialized = true;
    
    ESP_LOGI(TAG, "Scheduler initialized (max tasks: %d)", SCHEDULER_MAX_TASKS);
    return ESP_OK;
}

esp_err_t scheduler_start(void) {
    if (!s_scheduler.initialized) {
        return ESP_FAIL;
    }
    
    if (s_scheduler.running) {
        return ESP_OK;
    }
    
#ifdef ESP_PLATFORM
    // Create ESP32 timer (1 second periodic)
    const esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .name = "scheduler"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &s_scheduler.timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer");
        return ret;
    }
    
    ret = esp_timer_start_periodic(s_scheduler.timer, 1000000);  // 1 second
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer");
        return ret;
    }
#else
    // Create Linux thread
    s_scheduler.thread_running = true;
    if (pthread_create(&s_scheduler.thread, NULL, thread_func, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to create thread");
        s_scheduler.thread_running = false;
        return ESP_FAIL;
    }
#endif
    
    s_scheduler.running = true;
    ESP_LOGI(TAG, "Scheduler started");
    return ESP_OK;
}

esp_err_t scheduler_stop(void) {
    if (!s_scheduler.running) {
        return ESP_OK;
    }
    
#ifdef ESP_PLATFORM
    if (s_scheduler.timer) {
        esp_timer_stop(s_scheduler.timer);
        esp_timer_delete(s_scheduler.timer);
        s_scheduler.timer = NULL;
    }
#else
    s_scheduler.thread_running = false;
    pthread_join(s_scheduler.thread, NULL);
#endif
    
    s_scheduler.running = false;
    ESP_LOGI(TAG, "Scheduler stopped");
    return ESP_OK;
}

esp_err_t scheduler_deinit(void) {
    scheduler_stop();
    s_scheduler.task_count = 0;
    s_scheduler.initialized = false;
    ESP_LOGI(TAG, "Scheduler deinitialized");
    return ESP_OK;
}

bool scheduler_is_running(void) {
    return s_scheduler.running;
}

// ============================================================================
// Persistence
// ============================================================================

#ifdef ESP_PLATFORM
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static const char *action_type_to_string(scheduler_action_type_t type) {
    switch (type) {
        case SCHEDULER_ACTION_NONE: return "none";
        case SCHEDULER_ACTION_SCENE_RECALL: return "scene_recall";
        case SCHEDULER_ACTION_GROUP_CONTROL: return "group_control";
        case SCHEDULER_ACTION_RELAY_CONTROL: return "relay_control";
        case SCHEDULER_ACTION_MQTT_PUBLISH: return "mqtt_publish";
        case SCHEDULER_ACTION_HTTP_REQUEST: return "http_request";
        default: return "unknown";
    }
}

// TODO: Implement action_type_from_string when load_tasks JSON parsing is added
// static scheduler_action_type_t action_type_from_string(const char *str) { ... }

esp_err_t scheduler_save_tasks(const char *filepath) {
    if (!filepath) filepath = "/spiffs/scheduler.json";
    
    FILE *f = fopen(filepath, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", filepath);
        return ESP_FAIL;
    }
    
    fprintf(f, "{\"tasks\":[\n");
    
    for (int i = 0; i < s_scheduler.task_count; i++) {
        scheduler_task_t *t = &s_scheduler.tasks[i];
        if (i > 0) fprintf(f, ",\n");
        
        fprintf(f, "{\"id\":%d,\"name\":\"%s\",\"type\":%d,\"priority\":%d,\"enabled\":%s",
                t->task_id, t->name, t->type, t->priority, t->enabled ? "true" : "false");
        
        // Schedule details
        if (t->type == SCHEDULE_INTERVAL) {
            fprintf(f, ",\"interval\":%"PRIu32, t->schedule.interval_seconds);
        } else if (t->type == SCHEDULE_CRON) {
            fprintf(f, ",\"cron\":\"%s\"", t->cron_string);
        } else if (t->type == SCHEDULE_ONCE) {
            fprintf(f, ",\"run_time\":%ld", (long)t->schedule.once_time);
        }
        
        // Action
        if (t->action.type != SCHEDULER_ACTION_NONE) {
            fprintf(f, ",\"action\":{\"type\":\"%s\"", action_type_to_string(t->action.type));
            
            switch (t->action.type) {
                case SCHEDULER_ACTION_SCENE_RECALL:
                    fprintf(f, ",\"scene_id\":%d", t->action.params.scene.scene_id);
                    break;
                case SCHEDULER_ACTION_GROUP_CONTROL:
                    fprintf(f, ",\"group_id\":%d,\"on\":%s,\"level\":%d",
                            t->action.params.group.group_id,
                            t->action.params.group.on ? "true" : "false",
                            t->action.params.group.level);
                    break;
                case SCHEDULER_ACTION_RELAY_CONTROL:
                    fprintf(f, ",\"relay_id\":%d,\"on\":%s",
                            t->action.params.relay.relay_id,
                            t->action.params.relay.on ? "true" : "false");
                    break;
                case SCHEDULER_ACTION_MQTT_PUBLISH:
                    fprintf(f, ",\"topic\":\"%s\",\"payload\":\"%s\"",
                            t->action.params.mqtt.topic,
                            t->action.params.mqtt.payload);
                    break;
                case SCHEDULER_ACTION_HTTP_REQUEST:
                    fprintf(f, ",\"url\":\"%s\",\"method\":\"%s\"",
                            t->action.params.http.url,
                            t->action.params.http.method);
                    break;
                default:
                    break;
            }
            
            fprintf(f, "}");
        }
        
        fprintf(f, "}");
    }
    
    fprintf(f, "\n]}\n");
    fclose(f);
    
    ESP_LOGI(TAG, "Saved %d tasks to %s", s_scheduler.task_count, filepath);
    return ESP_OK;
}

esp_err_t scheduler_load_tasks(const char *filepath) {
    if (!filepath) filepath = "/spiffs/scheduler.json";
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGI(TAG, "No saved tasks file at %s", filepath);
        return ESP_OK;  // Not an error - first run
    }
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for reading", filepath);
        return ESP_FAIL;
    }
    
    // Read entire file into buffer
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    fread(json_str, 1, fsize, f);
    json_str[fsize] = 0;
    fclose(f);
    
    // Parse JSON (simple parser - could use cJSON if available)
    // For now, just log success
    ESP_LOGI(TAG, "Loaded tasks from %s (%ld bytes)", filepath, fsize);
    
    // TODO: Parse JSON and recreate tasks
    // This requires cJSON integration or a custom JSON parser
    
    free(json_str);
    return ESP_OK;
}

#else
// Non-ESP32 stubs
esp_err_t scheduler_save_tasks(const char *filepath) {
    ESP_LOGW(TAG, "Persistence not implemented for non-ESP32 platforms");
    return ESP_OK;
}

esp_err_t scheduler_load_tasks(const char *filepath) {
    ESP_LOGW(TAG, "Persistence not implemented for non-ESP32 platforms");
    return ESP_OK;
}
#endif
