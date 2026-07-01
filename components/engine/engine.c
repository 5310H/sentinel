#include "engine.h"
#include "../camera/camera_mgr.h"   // Camera snapshots on alarm
#include "../camera/camera_zones.h" // Camera zone processing
#include "../security/audit_log.h"
#include "dispatcher.h"
#include "hal.h"
#include "logging.h"
#include "storage_mgr.h"
#include "system_monitoring.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "../tuya/tuya_zones.h"
#endif
#include "../esphome_api/esphome_zones.h"

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

// External display update function (implemented in main.c)
extern void display_update_status(const char *state_text,
                                  const char *detail_text);

static const char *TAG = "ENGINE";
#else
#define ESP_LOGI(tag, format, ...)                                             \
  printf("[%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...)                                             \
  printf("[%s] WARNING: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...)                                             \
  printf("[%s] ERROR: " format "\n", tag, ##__VA_ARGS__)
static const char *TAG = "ENGINE";
#endif

// --- Global State Variables ---
int s_arm_state = 0; // 0: Disarmed, 1: Exit, 2: Armed, 3: Entry, 4: Alarmed
int s_timer = 0;
int s_chime_timer = 0;
arm_mode_t s_current_mode = ARM_AWAY;
static uint32_t s_armed_ticks = 0; // Tracks ticks since entering Armed state
static uint32_t s_esphome_offline_ticks[32] = {0}; // Watchdog counters
static uint32_t s_camera_offline_ticks[CAMERA_MAX_DEVICES] = {
    0};                                      // Camera watchdog
static uint32_t s_network_offline_ticks = 0; // Network watchdog

extern bool g_network_up;

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
static const uint64_t AUTH_LOCKOUT_MS = 300000; // 5 minute lockout
static const uint64_t AUTH_RESET_WINDOW_MS =
    60000; // Reset counter after 1 minute

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
    for (int r = 0; r < storage_get_relay_count(); r++) {
        if (strcmp(storage_get_relay(r)->type, "alarm") == 0 ||
strcmp(storage_get_relay(r)->type, "siren") == 0) {
            hal_set_relay(storage_get_relay(r)->gpio, state);
        }
    }
}
*/
// --- HELPER FUNCTIONS ---

static int get_zone_index_by_name(const char *name) {
  storage_lock();
  for (int i = 0; i < storage_get_zone_count(); i++) {
    if (strcmp(storage_get_zone(i)->name, name) == 0) {
      storage_unlock();
      return i;
    }
  }
  storage_unlock();
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
  tuya_zones_init();

  // Restore previous arm state from NVS (survives reboot)
  s_arm_state = engine_restore_arm_state();

  s_timer = 0;
  s_chime_timer = 0;
  s_current_mode = ARM_AWAY;
  strcpy(s_violation_name, "None");
  strcpy(s_violation_type, "None");

#ifdef ESP_PLATFORM
  // Configure KC868 Inputs (Zones)
  for (int i = 0; i < storage_get_zone_count(); i++) {
    // Validate GPIO number before using it
    if (storage_get_zone(i)->gpio < 0 || storage_get_zone(i)->gpio > 49) {
      ESP_LOGW(TAG, "Invalid zone GPIO %d at index %d",
               storage_get_zone(i)->gpio, i);
      continue;
    }
    gpio_reset_pin(storage_get_zone(i)->gpio);
    gpio_set_direction(storage_get_zone(i)->gpio, GPIO_MODE_INPUT);
    gpio_pullup_en(storage_get_zone(i)->gpio);
  }
  // Configure KC868 Outputs (Relays)
  for (int i = 0; i < storage_get_relay_count(); i++) {
    // Validate GPIO number before using it
    if (storage_get_relay(i)->gpio < 0 || storage_get_relay(i)->gpio > 49) {
      ESP_LOGW(TAG, "Invalid relay GPIO %d at index %d",
               storage_get_relay(i)->gpio, i);
      continue;
    }
    gpio_reset_pin(storage_get_relay(i)->gpio);
    gpio_set_direction(storage_get_relay(i)->gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(storage_get_relay(i)->gpio, 0);
  }
#endif

  // Initialize virtual zone monitoring
  esphome_zones_init();

  printf("[%s] System Logic Initialized. Mode: DISARMED\n", TAG);
}

// --- KEYPAD AUTHENTICATION ---

const char *engine_get_user_totp_secret(const char *username) {
  if (!username)
    return NULL;

  static char safe_secret[STR_MEDIUM] = {0};
  bool found = false;

  storage_lock();
  // Check users array
  for (int i = 0; i < storage_get_user_count() && i < MAX_USERS; i++) {
    if (strcmp(storage_get_user(i)->name, username) == 0) {
      // Return secret if set, otherwise NULL
      if (storage_get_user(i)->totp_secret[0] != '\0') {
        strncpy(safe_secret, storage_get_user(i)->totp_secret, STR_MEDIUM - 1);
        safe_secret[STR_MEDIUM - 1] = '\0';
        found = true;
      }
      break;
    }
  }
  storage_unlock();

  // Master user uses default secret if no per-user secret
  if (strcmp(username, "Master") == 0) {
    return NULL;
  }

  return found ? safe_secret : NULL;
}

keypad_result_t engine_check_keypad(const char *input_pin) {
  keypad_result_t res = {false, false, "Unknown"};
  if (!input_pin || strlen(input_pin) == 0)
    return res;

#ifdef ESP_PLATFORM
  uint64_t now_ms = esp_timer_get_time() / 1000;
#else
  uint64_t now_ms = 0; // Mock: no time on Linux
#endif

  // Rate limiting: Check if locked out
  if (auth_state.locked_out) {
    if (now_ms < auth_state.lockout_until_ms) {
      LOG_WARN(TAG, "Authentication locked out (attempt %" PRIu32 "/%" PRIu32 ")",
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

  storage_lock();
  bool is_master = (strcmp(input_pin, storage_get_config()->pin) == 0);
  storage_unlock();

  // Try master PIN
  if (is_master) {
    res.authenticated = true;
    res.is_admin = true;
    strcpy(res.name, "Master");
    auth_state.failed_attempts = 0; // Reset on success
    LOG_INFO(TAG, "Master PIN authenticated");
    return res;
  }

  // Try user PINs using centralized authentication
  user_t *user = NULL;
  if (user_authenticate_pin(input_pin, &user) && user != NULL) {
    res.authenticated = true;
    res.is_admin = user->is_admin;
    strncpy(res.name, user->name, sizeof(res.name) - 1);
    res.name[sizeof(res.name) - 1] = '\0'; // Ensure null termination
    auth_state.failed_attempts = 0;        // Reset on success

    // Audit log successful authentication
    audit_log_write(user->name, "", "LOGIN_SUCCESS", "engine_authenticate",
                    true, "Plaintext PIN");

    LOG_INFO(TAG, "User %s authenticated", user->name);

    return res;
  }

  // Authentication failed - increment counter
  auth_state.failed_attempts++;
  auth_state.last_attempt_ms = now_ms;

  // Audit log failed authentication
  char details[64];
  snprintf(details, sizeof(details), "Failed attempt %" PRIu32 "/%" PRIu32,
           auth_state.failed_attempts, MAX_AUTH_ATTEMPTS);
  audit_log_write("unknown", "", "LOGIN_FAILED", "engine_authenticate", false,
                  details);

  if (auth_state.failed_attempts >= MAX_AUTH_ATTEMPTS) {
    auth_state.locked_out = true;
    auth_state.lockout_until_ms = now_ms + AUTH_LOCKOUT_MS;
    LOG_ERROR(TAG, "Too many failed auth attempts! Locked out for 5 minutes");
    audit_log_write("system", "", "AUTH_LOCKOUT", "engine_authenticate", true,
                    "5 minute lockout due to repeated failures");
  } else {
    LOG_WARN(TAG, "Invalid PIN attempt (%" PRIu32 "/%" PRIu32 ")", auth_state.failed_attempts,
             MAX_AUTH_ATTEMPTS);
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
  storage_lock();
  for (int r = 0; r < storage_get_relay_count() && r < MAX_RELAYS; r++) {
    relay_type_t relay_type =
        relay_type_from_string(storage_get_relay(r)->type);
    if (relay_type == RELAY_TYPE_ALARM || relay_type == RELAY_TYPE_SIREN) {
      if (storage_get_relay(r)->gpio >= 0) {
        hal_set_relay(storage_get_relay(r)->gpio, true);
      }
    }
  }
  storage_unlock();
  hal_set_siren(true);
}

void process_alarm_event(int index) {
  storage_lock();
  if (index < 0 || index >= storage_get_zone_count() || index >= MAX_ZONES) {
    storage_unlock();
    return;
  }

  if (!storage_get_zone(index)->is_alert_sent) {
    printf("[%s] ALERT! %s violated.\n", TAG, storage_get_zone(index)->name);
    strncpy(s_violation_name, storage_get_zone(index)->name,
            sizeof(s_violation_name) - 1);
    strncpy(s_violation_type, storage_get_zone(index)->type,
            sizeof(s_violation_type) - 1);

#ifdef ESP_PLATFORM
    // Show alarm on display (truncate zone name if needed)
    char alarm_msg[80];
    snprintf(alarm_msg, sizeof(alarm_msg), "ALARM! %s",
             storage_get_zone(index)->name);
    display_update_status("*** ALARM ***", alarm_msg);
#endif

    trigger_local_hardware(storage_get_zone(index));
    dispatcher_alert(storage_get_zone(index));
    storage_get_zone(index)->is_alert_sent = true;

    // Trigger camera snapshots on alarm
    ESP_LOGI(TAG, "Triggering alarm snapshots for zone %d", index);
    int snapshots_captured = camera_mgr_trigger_alarm_snapshots(index);
    if (snapshots_captured > 0) {
      ESP_LOGI(TAG, "Captured %d alarm snapshot(s)", snapshots_captured);
    }
  }
  storage_unlock();
}

void engine_trigger_alarm(int trigger_index) {
  if (s_arm_state != 4) {
    s_arm_state = 4;
    engine_save_arm_state(4); // Persist alarm state
    LOG_ERROR(TAG, "STATE -> 4 (ALARMED)");
    printf("\n!!! ALARM TRIGGERED !!! Zone Index: %d\n", trigger_index);
  }
  process_alarm_event(trigger_index);
}

void engine_process_zone_trip(int i) {
  storage_lock();
  if (i < 0 ||
      (i >= storage_get_zone_count() && !camera_zones_is_camera_zone(i))) {
    storage_unlock();
    return;
  }

  // Handle camera zones differently
  if (camera_zones_is_camera_zone(i)) {
    LOG_INFO(TAG, "Camera zone trip processing: zone %d", i);
    // For camera zones, always trigger alarm if armed (no entry delay)
    if (s_arm_state == 2 || s_arm_state == 3) { // Armed away or entry
      engine_trigger_alarm(i);
    }
    storage_unlock();
    return;
  }

  LOG_INFO(TAG, "Zone trip processing: %s (GPIO %d)", storage_get_zone(i)->name,
           storage_get_zone(i)->gpio);

  // 24-HOUR ZONES (Always Active)
  if (!storage_get_zone(i)->is_alarm_on_armed_only) {
    zone_type_t ztype = zone_type_from_string(storage_get_zone(i)->type);
    if (ztype == ZONE_TYPE_FIRE || ztype == ZONE_TYPE_PANIC) {
      engine_trigger_alarm(i);
    }
    storage_unlock();
    return;
  }

  // CHIME (Only when Disarmed)
  if (s_arm_state == 0 && storage_get_zone(i)->is_chime && s_chime_timer == 0) {
    for (int r = 0; r < storage_get_relay_count() && r < MAX_RELAYS; r++) {
      relay_type_t rtype = relay_type_from_string(storage_get_relay(r)->type);
      if (rtype == RELAY_TYPE_CHIME || rtype == RELAY_TYPE_BELL) {
        hal_set_relay(storage_get_relay(r)->gpio, true);
      }
    }
    s_chime_timer = 20; // Approx 1 second @ 20Hz tick
    LOG_INFO(TAG, "CHIME: %s", storage_get_zone(i)->name);
  }

  // ARMING LOGIC (Entry Delay vs Instant Alarm)
  if (s_arm_state == 2) {
    // Grace period: Ignore interior sensors for the first 5 seconds after
    // arming to allow long-hold motion sensors (like ESPHome) to clear. (100
    // ticks @ 50ms = 5s)
    if (storage_get_zone(i)->is_interior && s_armed_ticks < 100) {
      LOG_INFO(TAG, "Ignoring interior trip on %s (Grace period settling)",
               storage_get_zone(i)->name);
      storage_unlock();
      return;
    }

    bool monitor = false;
    if (s_current_mode == ARM_AWAY || s_current_mode == ARM_VACATION)
      monitor = true;
    else if (s_current_mode == ARM_STAY && storage_get_zone(i)->is_perimeter)
      monitor = true;
    else if (s_current_mode == ARM_NIGHT &&
             (storage_get_zone(i)->is_perimeter ||
              !storage_get_zone(i)->is_interior))
      monitor = true;

    if (monitor) {
      strncpy(s_violation_name, storage_get_zone(i)->name,
              sizeof(s_violation_name) - 1);
      strncpy(s_violation_type, storage_get_zone(i)->type,
              sizeof(s_violation_type) - 1);

      if (storage_get_zone(i)->is_perimeter) {
        s_arm_state = 3;
        s_timer = storage_get_config()->entry_delay;
        engine_save_arm_state(3); // Persist entry mode
        LOG_WARN(TAG, "STATE -> 3 (ENTRY DELAY) triggered by: %s",
                 storage_get_zone(i)->name);
      } else {
        engine_trigger_alarm(i);
      }
    }
  }
  storage_unlock();
}

// --- CORE TICK LOGIC ---

void engine_tick(void) {
  static uint32_t heartbeat = 0;
  if (++heartbeat % 1200 ==
      0) { // Every ~60 seconds (assuming 50ms tick on ESP32)
    LOG_INFO(TAG, "Engine heartbeat - State: %d, Mode: %d", s_arm_state,
             s_current_mode);
  }

  // 0. Update system monitoring (tamper, power loss, temperature)
  static uint32_t mon_tick_counter = 0;
  if (mon_tick_counter++ % 10 == 0) { // Every 1 second (10 x 100ms)
    system_status_t status = {0};
    if (system_monitoring_update(&status) == ESP_OK) {
      // Log significant state changes
      if (status.tamper_detected) {
        LOG_ERROR(TAG, "TAMPER ALERT - Case opened!");
        if (s_arm_state == 2 || s_arm_state == 3) {
          snprintf(s_violation_name, sizeof(s_violation_name),
                   "Panel Case Tamper");
          strncpy(s_violation_type, "Tamper", sizeof(s_violation_type) - 1);
          engine_trigger_alarm(0); // Trigger physical alarm immediately
        }
      }
      if (status.on_backup_power) {
        LOG_WARN(TAG, "System on backup power - %" PRIu32 " ms",
                 status.backup_power_duration_ms);
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
      for (int r = 0; r < storage_get_relay_count(); r++) {

        if (strcmp(storage_get_relay(r)->type, "chime") == 0)
          hal_set_relay(storage_get_relay(r)->gpio, false);
      }
    }
  }

  // 1b. ESPHome Connection Watchdog & Armed Ticks
  if (s_arm_state == 2 || s_arm_state == 3) {
    s_armed_ticks++;

    // ESPHome Watchdog
    storage_lock();
    for (int i = 0; i < storage_get_esphome_count() && i < 32; i++) {
      esphome_device_t *dev = storage_get_esphome_device(i);
      if (dev && dev->enabled) {
        if (!dev->is_connected) {
          s_esphome_offline_ticks[i]++;
          uint32_t esphome_timeout_ticks =
              storage_get_config()->watchdog_esphome_sec *
              20; // 20 ticks per sec
          if (s_esphome_offline_ticks[i] == esphome_timeout_ticks) {
            LOG_ERROR(TAG,
                      "ESPHome device '%s' dropped offline while armed! "
                      "Triggering Tamper Alarm.",
                      dev->friendly_name);
            snprintf(s_violation_name, sizeof(s_violation_name), "%.48s (Offline)",
                     dev->friendly_name);
            strncpy(s_violation_type, "Tamper", sizeof(s_violation_type) - 1);

            // Find the virtual zone index to trigger the alarm
            int trigger_idx = 0;
            for (int z = 0; z < storage_get_zone_count(); z++) {
              if (storage_get_zone(z)->id == dev->virtual_zone_start) {
                trigger_idx = z;
                break;
              }
            }
            engine_trigger_alarm(trigger_idx);
          }
        } else {
          s_esphome_offline_ticks[i] = 0;
        }
      }
    }
    storage_unlock();

    // 1c. Camera Connection Watchdog
    storage_lock();
    for (int i = 0; i < camera_mgr_get_count() && i < CAMERA_MAX_DEVICES; i++) {
      const camera_device_t *cam = camera_mgr_get_device(i);
      if (cam && cam->enabled) {
        // If a camera fails 3 consecutive health checks (approx 90 seconds if
        // checked every 30s)
        if (cam->consecutive_failures >=
            storage_get_config()->watchdog_camera_fails) {
          s_camera_offline_ticks[i]++;
          if (s_camera_offline_ticks[i] ==
              1) { // Trigger only once when it crosses threshold
            LOG_ERROR(TAG,
                      "Camera '%s' dropped offline while armed! Triggering "
                      "Tamper Alarm.",
                      cam->friendly_name);
            snprintf(s_violation_name, sizeof(s_violation_name), "%.48s (Offline)",
                     cam->friendly_name);
            strncpy(s_violation_type, "Tamper", sizeof(s_violation_type) - 1);
            engine_trigger_alarm(0); // Trigger physical alarm immediately
          }
        } else {
          s_camera_offline_ticks[i] = 0;
        }
      }
    }
    storage_unlock();

    // 1d. Network Connection Watchdog (Ethernet Cable Cut)
    if (!g_network_up) {
      s_network_offline_ticks++;
      storage_lock();
      uint32_t net_timeout_ticks =
          storage_get_config()->watchdog_network_sec * 20;
      storage_unlock();
      if (s_network_offline_ticks == net_timeout_ticks) {
        LOG_ERROR(TAG, "Main Network Connection lost while armed! Triggering "
                       "Tamper Alarm.");
        snprintf(s_violation_name, sizeof(s_violation_name),
                 "Network Connection");
        strncpy(s_violation_type, "Tamper", sizeof(s_violation_type) - 1);
        engine_trigger_alarm(
            0); // Trigger physical alarm immediately (local siren)
      }
    } else {
      s_network_offline_ticks = 0;
    }

  } else {
    s_armed_ticks = 0;
    memset(s_esphome_offline_ticks, 0, sizeof(s_esphome_offline_ticks));
    memset(s_camera_offline_ticks, 0, sizeof(s_camera_offline_ticks));
    s_network_offline_ticks = 0;
  }

  // 2. State Transitions
  if (s_arm_state == 1 && s_timer == 0) {
    s_arm_state = 2;          // Exit -> Armed
    engine_save_arm_state(2); // Persist state
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
    storage_lock();
    int exit_delay_val = storage_get_config()->exit_delay;
    storage_unlock();
    s_timer = exit_delay_val;
    engine_save_arm_state(1); // Persist to NVS
    LOG_INFO(TAG, "Arming system (mode: %d, exit delay: %d)", mode,
             exit_delay_val);
  }
}

void engine_ui_disarm(void) {
  LOG_INFO(TAG, "Disarming System...");
  s_arm_state = 0;
  s_timer = 0;
  s_chime_timer = 0;
  engine_save_arm_state(0); // Persist to NVS

  hal_set_siren(false);
  storage_lock();
  for (int i = 0; i < storage_get_zone_count(); i++)
    storage_get_zone(i)->is_alert_sent = false;
  for (int r = 0; r < storage_get_relay_count(); r++)
    hal_set_relay(storage_get_relay(r)->gpio, false);
  storage_unlock();
}

int engine_get_arm_state(void) { return s_arm_state; }

const char *engine_get_violation_name(void) { return s_violation_name; }

const char *engine_get_violation_type(void) { return s_violation_type; }

bool is_ready(void) {
  // A system is "Ready" to arm only if no perimeter zones are tripped
  storage_lock();
  for (int i = 0; i < storage_get_zone_count() && i < MAX_ZONES; i++) {
    if (storage_get_zone(i)->is_perimeter) {
      bool zone_tripped = false;

      // Check GPIO-based zones
      if (storage_get_zone(i)->gpio != -1) {
        zone_tripped = (hal_get_zone_state(storage_get_zone(i)->gpio) == 0);
      }
      // Check virtual zones
      else {
#ifdef ESP_PLATFORM
        // Check ESPHome virtual zones
        if (esphome_is_zone_id_valid(storage_get_zone(i)->id)) {
          zone_tripped = esphome_zone_get_state(storage_get_zone(i)->id);
        }
        // Check Tuya virtual zones
        else if (tuya_is_zone_id_valid(storage_get_zone(i)->id)) {
          zone_tripped = tuya_zone_get_state(storage_get_zone(i)->id);
        }
#endif
      }

      if (zone_tripped) {
        storage_unlock();
        return false; // Found a door/window open
      }
    }
  }
  storage_unlock();
  return true; // All clear
}

// --- THREAD-SAFE STATE ACCESSORS ---

int engine_get_arm_state_safe(void) {
  int state = s_arm_state;
#ifdef ESP_PLATFORM
  if (engine_state_mutex != NULL) {
    if (xSemaphoreTakeRecursive(engine_state_mutex, pdMS_TO_TICKS(50)) ==
        pdTRUE) {
      state = s_arm_state;
      xSemaphoreGiveRecursive(engine_state_mutex);
    }
  }
#endif
  return state;
}

void engine_set_arm_state_safe(int state) {
  int old_state = s_arm_state;

#ifdef ESP_PLATFORM
  if (engine_state_mutex != NULL) {
    if (xSemaphoreTakeRecursive(engine_state_mutex, pdMS_TO_TICKS(50)) ==
        pdTRUE) {
      s_arm_state = state;
      xSemaphoreGiveRecursive(engine_state_mutex);
    }
  } else {
    s_arm_state = state;
  }
#else
  s_arm_state = state;
#endif

// Update display based on state
#ifdef ESP_PLATFORM
  const char *state_text = NULL;
  switch (state) {
  case 0:
    state_text = "DISARMED";
    break;
  case 1:
    // Display current mode during exit delay
    if (s_current_mode == ARM_STAY)
      state_text = "ARMING STAY";
    else if (s_current_mode == ARM_NIGHT)
      state_text = "ARMING NIGHT";
    else if (s_current_mode == ARM_VACATION)
      state_text = "ARMING VACATION";
    else
      state_text = "ARMING AWAY";
    break;
  case 2:
    // Display current mode when armed
    if (s_current_mode == ARM_STAY)
      state_text = "ARMED STAY";
    else if (s_current_mode == ARM_NIGHT)
      state_text = "ARMED NIGHT";
    else if (s_current_mode == ARM_VACATION)
      state_text = "ARMED VACATION";
    else
      state_text = "ARMED AWAY";
    break;
  case 3:
    state_text = "ENTRY DELAY";
    break;
  case 4:
    state_text = "*** ALARM ***";
    break;
  default:
    state_text = "UNKNOWN";
    break;
  }
  display_update_status(state_text, NULL);
#endif

  // Persist to NVS
  engine_save_arm_state(state);

  // Audit log state changes
  const char *state_names[] = {"DISARMED", "EXITING", "ARMED", "ENTRY_DELAY",
                               "ALARMED"};
  const char *old_state_str =
      (old_state >= 0 && old_state < 5) ? state_names[old_state] : "UNKNOWN";
  const char *new_state_str =
      (state >= 0 && state < 5) ? state_names[state] : "UNKNOWN";

  char details[128];
  const char *mode_names[] = {"AWAY", "STAY", "NIGHT", "VACATION"};
  const char *mode_str = (s_current_mode >= 0 && s_current_mode < 4)
                             ? mode_names[s_current_mode]
                             : "UNKNOWN";
  snprintf(details, sizeof(details), "%s -> %s (mode: %s)", old_state_str,
           new_state_str, mode_str);
  audit_log_write("system", "", "ARM_STATE_CHANGE", "engine", true, details);
}

int engine_get_timer_safe(void) {
  int timer = s_timer;
#ifdef ESP_PLATFORM
  if (engine_state_mutex != NULL) {
    if (xSemaphoreTakeRecursive(engine_state_mutex, pdMS_TO_TICKS(50)) ==
        pdTRUE) {
      timer = s_timer;
      xSemaphoreGiveRecursive(engine_state_mutex);
    }
  }
#endif
  return timer;
}

void engine_set_timer_safe(int value) {
#ifdef ESP_PLATFORM
  if (engine_state_mutex != NULL) {
    if (xSemaphoreTakeRecursive(engine_state_mutex, pdMS_TO_TICKS(50)) ==
        pdTRUE) {
      s_timer = value;
      xSemaphoreGiveRecursive(engine_state_mutex);
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
  int32_t restored_state = 0; // Default: DISARMED

#ifdef ESP_PLATFORM
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("engine", NVS_READONLY, &nvs_handle);
  if (err == ESP_OK) {
    err = nvs_get_i32(nvs_handle, "arm_state", &restored_state);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
      LOG_INFO(TAG, "Restored arm state from NVS: %" PRId32, restored_state);
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