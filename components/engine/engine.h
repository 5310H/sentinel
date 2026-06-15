/**
 * @file engine.h
 * @brief Sentinel alarm system engine - Core state machine and logic
 * 
 * This module implements the main alarm system state machine with:
 * - Zone monitoring and event processing
 * - Arm/disarm state management with configurable delays
 * - Alarm triggering and hardware relay control
 * - Authentication via PIN codes
 * - NVS persistence for state recovery after reboot
 * - Thread-safe access via FreeRTOS mutexes
 */

#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>
#include "storage_mgr.h"
#include "system_monitoring.h"

#ifdef ESP_PLATFORM
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
    #include "esp_err.h"
#else
    /* Linux/Mock compatibility stubs */
    typedef void* SemaphoreHandle_t;
    typedef int esp_err_t;
    #define ESP_OK 0
#endif

/**
 * @enum arm_mode_t
 * @brief Alarm system arming modes
 */
typedef enum {
    ARM_AWAY = 0,   /**< Away mode: all zones monitored */
    ARM_STAY,       /**< Stay mode: perimeter only, interior ignored */
    ARM_NIGHT,      /**< Night mode: perimeter + specific interior zones */
    ARM_VACATION    /**< Vacation mode: all zones monitored, extended monitoring */
} arm_mode_t;

// 2. Then, define structures that might use types
typedef struct {
    bool authenticated;
    bool is_admin;
    char name[STR_SMALL];
} keypad_result_t;

// 3. Finally, declare the functions using those types

/**
 * @brief Initialize the engine and all zones/relays
 * 
 * Initializes GPIO pins for all configured zones and relays, creates the
 * state protection mutex, and restores previous arm state from NVS storage.
 * Must be called once during startup before engine_tick().
 */
void engine_init(void);

/**
 * @brief Main engine tick - Process one cycle of state machine
 * 
 * Called every 100ms by monitor_task(). Handles:
 * - Zone state monitoring with debouncing
 * - Timer countdowns (entry/exit delays)
 * - State transitions (Disarmed -> Exiting -> Armed -> Entry -> Alarmed)
 * - Alarm triggering and dispatcher notifications
 * - Hardware relay control (siren, strobe, chime)
 * 
 * Thread-safe via engine_state_mutex for concurrent access.
 */
void engine_tick(void);

/**
 * @brief Get TOTP secret for a user by name
 * 
 * @param username User's name
 * @return const char* TOTP secret (base32 encoded) or NULL if not found/not set
 */
const char* engine_get_user_totp_secret(const char* username);

/**
 * @brief Authenticate a user via PIN code
 * 
 * Verifies the input PIN against master PIN and user PIN list.
 * 
 * @param input_pin User-entered PIN code (must be null-terminated)
 * @return keypad_result_t Authentication result with user info
 */
keypad_result_t engine_check_keypad(const char* input_pin);

/**
 * @brief Arm the system in specified mode with exit delay
 * 
 * Initiates exit delay countdown (configurable 0-300s). During exit delay,
 * the system will transition: DISARMED -> EXITING -> ARMED.
 * 
 * @param mode Arm mode (ARM_AWAY, ARM_STAY, or ARM_NIGHT)
 */
void engine_ui_arm(arm_mode_t mode); 

/**
 * @brief Disarm the system
 * 
 * Immediately transitions system to DISARMED state, silencing alarms
 * and stopping all zone monitoring. Requires authentication via separate
 * keypad validation before calling.
 */
void engine_ui_disarm(void);

/**
 * @brief Trigger alarm for specified zone
 * 
 * Activates alarm hardware (siren, strobe) and notifies dispatcher
 * for professional monitoring. Only activates once per alarm event.
 * 
 * @param trigger_index Zone index that triggered the alarm
 */
void engine_trigger_alarm(int trigger_index);

/**
 * @brief Process a confirmed zone trip event
 * 
 * Handles logic for 24-hour zones (Fire/Panic), Chimes, and Arming logic.
 * Decides whether to trigger an immediate alarm, start entry delay, or ignore.
 * 
 * @param zone_index Index of the zone that was tripped
 */
void engine_process_zone_trip(int zone_index);

/**
 * @brief Get current arm state (thread-safe)
 * 
 * Safe getter protected by mutex. Returns:
 * - 0: DISARMED
 * - 1: EXITING (during exit delay)
 * - 2: ARMED (fully armed)
 * - 3: ENTRY (entry delay triggered)
 * - 4: ALARMED (active alarm)
 * 
 * @return int Current arm state (0-4)
 */
int engine_get_arm_state(void);

/**
 * @brief Set arm state (thread-safe with NVS persistence)
 * 
 * Safe setter protected by mutex. Saves to NVS for recovery after reboot.
 * 
 * @param state New arm state (0-4)
 */
void engine_set_arm_state_safe(int state);

/**
 * @brief Get timer countdown value (thread-safe)
 * 
 * Returns milliseconds remaining in current delay (entry or exit).
 * 
 * @return int Remaining time in deciseconds (0-3000 for 300s max)
 */
int engine_get_timer_safe(void);

/**
 * @brief Set timer countdown (thread-safe)
 * 
 * Sets delay timer in deciseconds (0.1s units).
 * 
 * @param value Timer value in deciseconds
 */
void engine_set_timer_safe(int value);

/**
 * @brief Check if system is ready to arm
 * 
 * Verifies all perimeter zones are secure (no open doors/windows).
 * 
 * @return true All perimeter zones secure
 * @return false One or more perimeter zones violated
 */
bool engine_is_ready(void);

/**
 * @brief Save current arm state to NVS (non-volatile storage)
 * 
 * Called automatically on state changes. Persists state for recovery
 * after power loss or reboot.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t engine_save_state_to_nvs(void);

/**
 * @brief Restore arm state from NVS after reboot
 * 
 * Called during engine_init(). Restores previous armed state so
 * system doesn't disarm unexpectedly after power loss.
 * 
 * @return int Restored arm state (0-4), or 0 if no saved state
 */
int engine_restore_state_from_nvs(void);

// External state variable for legacy code compatibility
extern int s_arm_state;

// State protection mutex
extern SemaphoreHandle_t engine_state_mutex;

// Thread-safe getters/setters
int engine_get_arm_state_safe(void);
void engine_set_arm_state_safe(int state);
int engine_get_timer_safe(void);
void engine_set_timer_safe(int value);

// NVS persistence
void engine_save_arm_state(int state);
int engine_restore_arm_state(void);

#endif