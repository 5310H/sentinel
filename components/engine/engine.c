#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include "engine.h"
#include "storage_mgr.h"
#include "dispatcher.h"
#include "hal.h"
#include "logging.h"
#include "system_monitoring.h"
#include "../security/password_hash.h"
#include "../security/audit_log.h"
#include "../camera/camera_mgr.h"  // Camera snapshots on alarm
#include "../camera/camera_zones.h"  // Camera zone processing

#ifdef ESP_PLATFORM
#include "../tuya/tuya_zones.h"
#endif
#include "../esphome_api/esphome_zones.h"

#ifdef ESP_PLATFORM
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "driver/gpio.h"
    #include "esp_log.h"
    #include "nvs_flash.h"
    #include "nvs.h"
    #include "esp_timer.h"
    
    // External display update function (implemented in main.c)
    extern void display_update_status(const char* state_text, const char* detail_text);
    
    static const char *TAG = "ENGINE";
#else
    #define ESP_LOGI(tag, format, ...) printf("[%s] " format "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, format, ...) printf("[%s] WARNING: " format "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGE(tag, format, ...) printf("[%s] ERROR: " format "\n", tag, ##__VA_ARGS__)
    static const char *TAG = "ENGINE";
#endif

// --- Global State Variables ---
int s_arm_state = 0;      // 0: Disarmed, 1: Exit, 2: Armed, 3: Entry, 4: Alarmed
int s_timer = 0;          
int s_chime_timer = 0;    
arm_mode_t s_current_mode = ARM_AWAY;

char s_violation_name[64] = "None";
char s_violation_type[32] = "None";

// Mutex for protecting state variables from race conditions
SemaphoreHandle_t engine_state_mutex = NULL;

// --- Authentication Rate Limiting ---
typedef struct {
    uint32_t failed_attempts;
    uint64_t last_attempt_ms;
    bool locked_out;
    uint64_t lockout_until_ms;
} auth_state_t;

static auth_state_t auth_state = {0, 0, false, 0};
static const uint32_t MAX_AUTH_ATTEMPTS = 5;
static const uint64_t AUTH_LOCKOUT_MS = 300000;  // 5 minute lockout
static const uint64_t AUTH_RESET_WINDOW_MS = 60000;  // Reset counter after 1 minute 

// --- HARDWARE ABSTRACTION LAYER (KC868 Bridge) ---
/*
int hal_get_zone_state(int gpio) {
#ifdef ESP_PLATFORM
    return gpio_get_level((gpio_num_t)gpio);
#else
    // Mock for Linux - assumes pull-up resistors (1 = closed/secure)
    return 1; 
#endif
}

void hal_set_relay(int gpio, bool state) {
#ifdef ESP_PLATFORM
    gpio_set_level((gpio_num_t)gpio, state ? 1 : 0);
#else
    printf("[HAL] Relay GPIO %d -> %s\n", gpio, state ? "ON" : "OFF");
#endif
}

void hal_set_siren(bool state) {
    // Find relay tagged as 'alarm' or use a dedicated siren pin
    for (int r = 0; r < r_count; r++) {
        if (strcmp(relays[r].type, "alarm") == 0 || strcmp(relays[r].type, "siren") == 0) {
            hal_set_relay(relays[r].gpio, state);
        }
    }
}
*/
// --- HELPER FUNCTIONS ---

static int get_zone_index_by_name(const char* name) {
    for (int i = 0; i < z_count; i++) {
        if (strcmp(zones[i].name, name) == 0) return i;
    }
    return -1;
}

// --- INITIALIZATION ---

void engine_init(void) {
    LOG_INFO(TAG, "Engine initializing...");
    // Create state protection mutex
    #ifdef ESP_PLATFORM
        if (engine_state_mutex == NULL) {
            engine_state_mutex = xSemaphoreCreateMutex();
        }
    #endif
    
    // Initialize system monitoring (RTC, tamper detection, power loss)
    if (system_monitoring_init(NULL) != ESP_OK) {
        LOG_WARN(TAG, "System monitoring initialization failed");
    }
    
    // Initialize virtual zone monitoring
    esphome_zones_init();
    // tuya_zones_init(); // Uncomment when Tuya zones are implemented
    
    // Restore previous arm state from NVS (survives reboot)
    s_arm_state = engine_restore_arm_state();
    
    s_timer = 0;
    s_chime_timer = 0;
    s_current_mode = ARM_AWAY;
    strcpy(s_violation_name, "None");
    strcpy(s_violation_type, "None");

#ifdef ESP_PLATFORM
    // Configure KC868 Inputs (Zones)
    for(int i = 0; i < z_count; i++) {
        // Validate GPIO number before using it
        if (zones[i].gpio < 0 || zones[i].gpio > 49) {
            ESP_LOGW(TAG, "Invalid zone GPIO %d at index %d", zones[i].gpio, i);
            continue;
        }
        gpio_reset_pin(zones[i].gpio);
        gpio_set_direction(zones[i].gpio, GPIO_MODE_INPUT);
        gpio_pullup_en(zones[i].gpio); 
    }
    // Configure KC868 Outputs (Relays)
    for(int i = 0; i < r_count; i++) {
        // Validate GPIO number before using it
        if (relays[i].gpio < 0 || relays[i].gpio > 49) {
            ESP_LOGW(TAG, "Invalid relay GPIO %d at index %d", relays[i].gpio, i);
            continue;
        }
        gpio_reset_pin(relays[i].gpio);
        gpio_set_direction(relays[i].gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(relays[i].gpio, 0);
    }
#endif

    // Initialize virtual zone monitoring
    esphome_zones_init();

    printf("[%s] System Logic Initialized. Mode: DISARMED\n", TAG);
}

// --- KEYPAD AUTHENTICATION ---

const char* engine_get_user_totp_secret(const char* username) {
    if (!username) return NULL;
    
    // Check users array
    for (int i = 0; i < u_count && i < MAX_USERS; i++) {
        if (strcmp(users[i].name, username) == 0) {
            // Return secret if set, otherwise NULL
            if (users[i].totp_secret[0] != '\0') {
                return users[i].totp_secret;
            }
            return NULL;
        }
    }
    
    // Master user uses default secret if no per-user secret
    if (strcmp(username, "Master") == 0) {
        return NULL;  // Or return default secret if desired
    }
    
    return NULL;
}

keypad_result_t engine_check_keypad(const char* input_pin) {
    keypad_result_t res = {false, false, "Unknown"};
    if (!input_pin || strlen(input_pin) == 0) return res;

    #ifdef ESP_PLATFORM
        uint64_t now_ms = esp_timer_get_time() / 1000;
    #else
        uint64_t now_ms = 0;  // Mock: no time on Linux
    #endif

    // Rate limiting: Check if locked out
    if (auth_state.locked_out) {
        if (now_ms < auth_state.lockout_until_ms) {
            LOG_WARN(TAG, "Authentication locked out (attempt %u/%u)", 
                auth_state.failed_attempts, MAX_AUTH_ATTEMPTS);
            return res;
        } else {
            // Lockout expired, reset
            auth_state.locked_out = false;
            auth_state.failed_attempts = 0;
        }
    }

    // Reset failed counter if outside window
    if (now_ms - auth_state.last_attempt_ms > AUTH_RESET_WINDOW_MS) {
        auth_state.failed_attempts = 0;
    }

    // Try master PIN
    if (strcmp(input_pin, config.pin) == 0) {
        res.authenticated = true;
        res.is_admin = true;
        strcpy(res.name, "Master");
        auth_state.failed_attempts = 0;  // Reset on success
        LOG_INFO(TAG, "Master PIN authenticated");
        return res; 
    } 
    
    // Try user PINs using centralized authentication
    user_t *user = NULL;
    if (user_authenticate_pin(input_pin, &user) && user != NULL) {
        res.authenticated = true;
        res.is_admin = user->is_admin; 
        strncpy(res.name, user->name, sizeof(res.name) - 1);
        res.name[sizeof(res.name) - 1] = '\0';  // Ensure null termination
        auth_state.failed_attempts = 0;  // Reset on success
        
        // Audit log successful authentication
        audit_log_write(user->name, "", "LOGIN_SUCCESS", "engine_authenticate", 
                       true, user->use_legacy_pin ? "Legacy plaintext PIN" : "Hashed PIN");
        
        LOG_INFO(TAG, "User %s authenticated (%s)", user->name, 
                 user->use_legacy_pin ? "legacy" : "hashed");
        
        return res; 
    }
    
    // Authentication failed - increment counter
    auth_state.failed_attempts++;
    auth_state.last_attempt_ms = now_ms;
    
    // Audit log failed authentication
    char details[64];
    snprintf(details, sizeof(details), "Failed attempt %" PRIu32 "/%" PRIu32, 
             auth_state.failed_attempts, MAX_AUTH_ATTEMPTS);
    audit_log_write("unknown", "", "LOGIN_FAILED", "engine_authenticate", false, details);
    
    if (auth_state.failed_attempts >= MAX_AUTH_ATTEMPTS) {
        auth_state.locked_out = true;
        auth_state.lockout_until_ms = now_ms + AUTH_LOCKOUT_MS;
        LOG_ERROR(TAG, "Too many failed auth attempts! Locked out for 5 minutes");
        audit_log_write("system", "", "AUTH_LOCKOUT", "engine_authenticate", 
                       true, "5 minute lockout due to repeated failures");
    } else {
        LOG_WARN(TAG, "Invalid PIN attempt (%u/%u)", 
            auth_state.failed_attempts, MAX_AUTH_ATTEMPTS);
    }
    
    return res; 
}

// --- ALARM PROCESSOR ---

void trigger_local_hardware(zone_t *z) {
    if (z == NULL) {
        LOG_ERROR(TAG, "Null zone pointer in trigger_local_hardware");
        return;
    }
    LOG_WARN(TAG, "Triggering hardware for zone: %s", z->name);
    for (int r = 0; r < r_count && r < MAX_RELAYS; r++) {
        relay_type_t relay_type = relay_type_from_string(relays[r].type);
        if (relay_type == RELAY_TYPE_ALARM || relay_type == RELAY_TYPE_SIREN) {
            if (relays[r].gpio >= 0) {
                hal_set_relay(relays[r].gpio, true);
            }
        }
    }
    hal_set_siren(true);
}

void process_alarm_event(int index) {
    if (index < 0 || index >= z_count || index >= MAX_ZONES) return;
    
    if (!zones[index].is_alert_sent) {
        printf("[%s] ALERT! %s violated.\n", TAG, zones[index].name);
        strncpy(s_violation_name, zones[index].name, sizeof(s_violation_name)-1);
        strncpy(s_violation_type, zones[index].type, sizeof(s_violation_type)-1);

        #ifdef ESP_PLATFORM
            // Show alarm on display (truncate zone name if needed)
            char alarm_msg[80];
            snprintf(alarm_msg, sizeof(alarm_msg), "ALARM! %s", zones[index].name);
            display_update_status("*** ALARM ***", alarm_msg);
        #endif

        trigger_local_hardware(&zones[index]);
        dispatcher_alert(&zones[index]); 
        zones[index].is_alert_sent = true;
        
        // Trigger camera snapshots on alarm
        ESP_LOGI(TAG, "Triggering alarm snapshots for zone %d", index);
        int snapshots_captured = camera_mgr_trigger_alarm_snapshots(index);
        if (snapshots_captured > 0) {
            ESP_LOGI(TAG, "Captured %d alarm snapshot(s)", snapshots_captured);
        } 
    }
}

void engine_trigger_alarm(int trigger_index) {
    if (s_arm_state != 4) { 
        s_arm_state = 4;
        engine_save_arm_state(4);  // Persist alarm state
        LOG_ERROR(TAG, "STATE -> 4 (ALARMED)");
        printf("\n!!! ALARM TRIGGERED !!! Zone Index: %d\n", trigger_index);
    }
    process_alarm_event(trigger_index);
}

void engine_process_zone_trip(int i) {
    if (i < 0 || (i >= z_count && !camera_zones_is_camera_zone(i))) return;

    // Handle camera zones differently
    if (camera_zones_is_camera_zone(i)) {
        LOG_INFO(TAG, "Camera zone trip processing: zone %d", i);
        // For camera zones, always trigger alarm if armed (no entry delay)
        if (s_arm_state == 2 || s_arm_state == 3) { // Armed away or entry
            engine_trigger_alarm(i);
        }
        return;
    }

    LOG_INFO(TAG, "Zone trip processing: %s (GPIO %d)", zones[i].name, zones[i].gpio);

    // 24-HOUR ZONES (Always Active)
    if (!zones[i].is_alarm_on_armed_only) {
        zone_type_t ztype = zone_type_from_string(zones[i].type);
        if (ztype == ZONE_TYPE_FIRE || ztype == ZONE_TYPE_PANIC) {
            engine_trigger_alarm(i);
        }
        return; 
    }

    // CHIME (Only when Disarmed)
    if (s_arm_state == 0 && zones[i].is_chime && s_chime_timer == 0) {
        for (int r = 0; r < r_count && r < MAX_RELAYS; r++) {
            relay_type_t rtype = relay_type_from_string(relays[r].type);
            if (rtype == RELAY_TYPE_CHIME || rtype == RELAY_TYPE_BELL) {
                hal_set_relay(relays[r].gpio, true);
            }
        }
        s_chime_timer = 20; // Approx 1 second @ 20Hz tick
        LOG_INFO(TAG, "CHIME: %s", zones[i].name);
    }
    
    // ARMING LOGIC (Entry Delay vs Instant Alarm)
    if (s_arm_state == 2) { 
        bool monitor = false;
        if (s_current_mode == ARM_AWAY || s_current_mode == ARM_VACATION) monitor = true;
        else if (s_current_mode == ARM_STAY && zones[i].is_perimeter) monitor = true;
        else if (s_current_mode == ARM_NIGHT && (zones[i].is_perimeter || !zones[i].is_interior)) monitor = true;

        if (monitor) {
            strncpy(s_violation_name, zones[i].name, sizeof(s_violation_name)-1);
            strncpy(s_violation_type, zones[i].type, sizeof(s_violation_type)-1);

            if (zones[i].is_perimeter) {
                s_arm_state = 3; 
                s_timer = config.entry_delay;
                engine_save_arm_state(3);  // Persist entry mode
                LOG_WARN(TAG, "STATE -> 3 (ENTRY DELAY) triggered by: %s", zones[i].name);
            } else {
                engine_trigger_alarm(i);
            }
        }
    }
}

// --- CORE TICK LOGIC ---

void engine_tick(void) {
    static uint32_t heartbeat = 0;
    if (++heartbeat % 6000 == 0) { // Every ~60 seconds (assuming 10ms tick in mock)
        LOG_INFO(TAG, "Engine heartbeat - State: %d, Mode: %d", s_arm_state, s_current_mode);
    }

    // 0. Update system monitoring (tamper, power loss, temperature)
    static uint32_t mon_tick_counter = 0;
    if (mon_tick_counter++ % 10 == 0) {  // Every 1 second (10 x 100ms)
        system_status_t status = {0};
        if (system_monitoring_update(&status) == ESP_OK) {
            // Log significant state changes
            if (status.tamper_detected) {
                LOG_ERROR(TAG, "TAMPER ALERT - Case opened!");
            }
            if (status.on_backup_power) {
                LOG_WARN(TAG, "System on backup power - %u ms", status.backup_power_duration_ms);
            }
            if (status.temperature_c > 45) {
                LOG_WARN(TAG, "High temperature: %d°C", status.temperature_c);
            }
        }
    }
    
    // 1. Manage Timers
    if (s_timer > 0) {
        s_timer--;
        if (s_timer % 20 == 0 || s_timer < 5) {
            printf("[TICK] State: %d, Time Left: %ds\n", s_arm_state, s_timer);
            LOG_INFO(TAG, "[TICK] State: %d, Time Left: %ds\n", s_arm_state, s_timer);
        }
    }

    if (s_chime_timer > 0) {
        s_chime_timer--;
        LOG_INFO(TAG, "Chime timer %d", s_chime_timer);
        if (s_chime_timer == 0) {
            for (int r = 0; r < r_count; r++) {

                if (strcmp(relays[r].type, "chime") == 0) hal_set_relay(relays[r].gpio, false);
            }
        }
    }

    // 2. State Transitions
    if (s_arm_state == 1 && s_timer == 0) {
        s_arm_state = 2; // Exit -> Armed
        engine_save_arm_state(2);  // Persist state
        LOG_INFO(TAG, "STATE -> 2 (ARMED)");
    }
    
    if (s_arm_state == 3 && s_timer == 0) {
        LOG_ERROR(TAG, "Entry delay expired. Triggering full alarm!");
        int idx = get_zone_index_by_name(s_violation_name);
        engine_trigger_alarm(idx != -1 ? idx : 0);
    }
}

// --- PUBLIC COMMANDS ---

void engine_ui_arm(arm_mode_t mode) {
    if (s_arm_state == 0) { 
        s_current_mode = mode;
        s_arm_state = 1; 
        s_timer = config.exit_delay;
        engine_save_arm_state(1);  // Persist to NVS
        LOG_INFO(TAG, "Arming system (mode: %d, exit delay: %d)", mode, config.exit_delay);
    }
}

void engine_ui_disarm(void) {
    LOG_INFO(TAG, "Disarming System...");
    s_arm_state = 0;
    s_timer = 0;
    s_chime_timer = 0;
    engine_save_arm_state(0);  // Persist to NVS
    
    hal_set_siren(false);
    for (int i = 0; i < z_count; i++) zones[i].is_alert_sent = false; 
    for (int r = 0; r < r_count; r++) hal_set_relay(relays[r].gpio, false);
}

int engine_get_arm_state(void) { 
    return s_arm_state; 
}

const char* engine_get_violation_name(void) { 
    return s_violation_name; 
}

const char* engine_get_violation_type(void) { 
    return s_violation_type; 
}

bool is_ready(void) {
    // A system is "Ready" to arm only if no perimeter zones are tripped
    for (int i = 0; i < z_count && i < MAX_ZONES; i++) {
        if (zones[i].is_perimeter) {
            bool zone_tripped = false;

            // Check GPIO-based zones
            if (zones[i].gpio != -1) {
                zone_tripped = (hal_get_zone_state(zones[i].gpio) == 0);
            }
            // Check virtual zones
            else {
#ifdef ESP_PLATFORM
                // Check ESPHome virtual zones
                if (esphome_is_zone_id_valid(zones[i].id)) {
                    zone_tripped = esphome_zone_get_state(zones[i].id);
                }
                // Check Tuya virtual zones
                else if (tuya_is_zone_id_valid(zones[i].id)) {
                    zone_tripped = tuya_zone_get_state(zones[i].id);
                }
#endif
            }

            if (zone_tripped) {
                return false; // Found a door/window open
            }
        }
    }
    return true; // All clear
}

// --- THREAD-SAFE STATE ACCESSORS ---

int engine_get_arm_state_safe(void) {
    int state = s_arm_state;
    #ifdef ESP_PLATFORM
        if (engine_state_mutex != NULL) {
            if (xSemaphoreTake(engine_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                state = s_arm_state;
                xSemaphoreGive(engine_state_mutex);
            }
        }
    #endif
    return state;
}

void engine_set_arm_state_safe(int state) {
    int old_state = s_arm_state;
    
    #ifdef ESP_PLATFORM
        if (engine_state_mutex != NULL) {
            if (xSemaphoreTake(engine_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_arm_state = state;
                xSemaphoreGive(engine_state_mutex);
            }
        } else {
            s_arm_state = state;
        }
    #else
        s_arm_state = state;
    #endif
    
    // Update display based on state
    #ifdef ESP_PLATFORM
        const char* state_text = NULL;
        switch(state) {
            case 0: state_text = "DISARMED"; break;
            case 1: 
                // Display current mode during exit delay
                if (s_current_mode == ARM_STAY) state_text = "ARMING STAY";
                else if (s_current_mode == ARM_NIGHT) state_text = "ARMING NIGHT";
                else if (s_current_mode == ARM_VACATION) state_text = "ARMING VACATION";
                else state_text = "ARMING AWAY";
                break;
            case 2:
                // Display current mode when armed
                if (s_current_mode == ARM_STAY) state_text = "ARMED STAY";
                else if (s_current_mode == ARM_NIGHT) state_text = "ARMED NIGHT";
                else if (s_current_mode == ARM_VACATION) state_text = "ARMED VACATION";
                else state_text = "ARMED AWAY";
                break;
            case 3: state_text = "ENTRY DELAY"; break;
            case 4: state_text = "*** ALARM ***"; break;
            default: state_text = "UNKNOWN"; break;
        }
        display_update_status(state_text, NULL);
    #endif
    
    // Persist to NVS
    engine_save_arm_state(state);
    
    // Audit log state changes
    const char *state_names[] = {"DISARMED", "EXITING", "ARMED", "ENTRY_DELAY", "ALARMED"};
    const char *old_state_str = (old_state >= 0 && old_state < 5) ? state_names[old_state] : "UNKNOWN";
    const char *new_state_str = (state >= 0 && state < 5) ? state_names[state] : "UNKNOWN";
    
    char details[128];
    const char *mode_names[] = {"AWAY", "STAY", "NIGHT", "VACATION"};
    const char *mode_str = (s_current_mode >= 0 && s_current_mode < 4) ? mode_names[s_current_mode] : "UNKNOWN";
    snprintf(details, sizeof(details), "%s -> %s (mode: %s)", old_state_str, new_state_str, mode_str);
    audit_log_write("system", "", "ARM_STATE_CHANGE", "engine", true, details);
}

int engine_get_timer_safe(void) {
    int timer = s_timer;
    #ifdef ESP_PLATFORM
        if (engine_state_mutex != NULL) {
            if (xSemaphoreTake(engine_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                timer = s_timer;
                xSemaphoreGive(engine_state_mutex);
            }
        }
    #endif
    return timer;
}

void engine_set_timer_safe(int value) {
    #ifdef ESP_PLATFORM
        if (engine_state_mutex != NULL) {
            if (xSemaphoreTake(engine_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_timer = value;
                xSemaphoreGive(engine_state_mutex);
            }
        } else {
            s_timer = value;
        }
    #else
        s_timer = value;
    #endif
}

// --- NVS PERSISTENCE ---

void engine_save_arm_state(int state) {
    #ifdef ESP_PLATFORM
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("engine", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_set_i32(nvs_handle, "arm_state", state);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            LOG_DEBUG(TAG, "Saved arm state to NVS: %d", state);
        } else {
            LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        }
    #else
        LOG_DEBUG(TAG, "NVS save (mock): arm_state = %d", state);
    #endif
}

int engine_restore_arm_state(void) {
    int32_t restored_state = 0;  // Default: DISARMED
    
    #ifdef ESP_PLATFORM
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("engine", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            err = nvs_get_i32(nvs_handle, "arm_state", &restored_state);
            nvs_close(nvs_handle);
            
            if (err == ESP_OK) {
                LOG_INFO(TAG, "Restored arm state from NVS: %d", restored_state);
            } else if (err == ESP_ERR_NVS_NOT_FOUND) {
                LOG_INFO(TAG, "No saved arm state in NVS, defaulting to DISARMED");
                restored_state = 0;
            } else {
                LOG_ERROR(TAG, "Error reading NVS: %s", esp_err_to_name(err));
                restored_state = 0;
            }
        } else {
            LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
            restored_state = 0;
        }
    #else
        LOG_DEBUG(TAG, "NVS restore (mock): defaulting to DISARMED");
    #endif
    
    return restored_state;
}