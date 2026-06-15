#ifndef MONITOR_H
#define MONITOR_H

/**
 * @file monitor.h
 * @brief Zone state monitoring and debouncing
 * 
 * Handles real-time zone polling, debouncing, and state change events.
 * Part of core alert detection pipeline:
 * monitor_task() → monitor_scan_all() → monitor_process_event() → engine_trigger_alarm()
 * 
 * Features:
 * - Per-zone debounce filtering (3 consecutive readings required)
 * - GPIO state transition detection
 * - Integration with engine state machine for arm/disarm
 * - Called every 50ms by monitor_task (FreeRTOS)
 * 
 * Note: Operates with zone_debounce_counter[] state maintained in engine module
 */

/**
 * @brief Initialize zone monitoring subsystem
 * 
 * Sets up GPIO inputs, initial debounce counters, and state cache.
 * Called once during app_main() initialization.
 * 
 * Dependencies:
 * - hal_init() must be called first
 * - storage_load_all() must have loaded zone configuration
 * 
 * @example
 * hal_init();
 * storage_load_all();
 * monitor_init();  // Ready for monitor_task to call monitor_scan_all()
 */
void monitor_init(void);

/**
 * @brief Poll all zones for state changes
 * 
 * Iterates through all configured zones, reads GPIO states,
 * applies debounce filtering (3 consecutive identical reads required),
 * and queues state changes for processing.
 * 
 * Called every 50ms by monitor_task (higher priority than mongoose_task).
 * Completes in < 5ms even with 32 zones (non-blocking design).
 * 
 * @example
 * // In monitor_task, every 50ms:
 * monitor_scan_all();
 */
void monitor_scan_all(void);

/**
 * @brief Process a specific zone state change event
 * 
 * When debounce filter confirms state change (3 consecutive reads),
 * this function is called to:
 * - Log the zone change
 * - Check if system should trigger alarm
 * - Update zone status for UI display
 * - Queue alert dispatcher if needed
 * 
 * Interacts with engine state machine:
 * - If ARMED: calls engine_trigger_alarm()
 * - If DISARMED: logs event for history only
 * - If EXITING/ENTERING: timer management
 * 
 * @param index Zone index (0-31 valid, bounds checked)
 * 
 * @example
 * if (debounce[2] == 3 && old_state[2] != new_state[2]) {
 *     monitor_process_event(2);  // Process zone 2 state change
 * }
 */
void monitor_process_event(int index);

#endif