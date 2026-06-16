#ifndef NOONLIGHT_H
#define NOONLIGHT_H

/**
 * @file noonlight.h
 * @brief Professional emergency dispatch integration via Noonlight API
 * 
 * Integrates with Noonlight's professional monitoring service for:
 * - Fire department dispatch (SLOT_FIRE)
 * - Police dispatch (SLOT_POLICE)
 * - Medical dispatch (SLOT_MED)
 * - Other emergency dispatch (SLOT_OTH)
 * 
 * Features:
 * - Alarm creation with location and zone info
 * - Event logging (zone triggers, user actions)
 * - Emergency contact management
 * - Instruction delivery to emergency responders
 * - Alarm cancellation with user verification
 * - HTTPS with OAuth2 Bearer token authentication
 * 
 * Note: Requires noonlight_api_key and noonlight_account_id in config.json
 */

#include "storage_mgr.h"

// Slot definitions for different emergency types
#define SLOT_POLICE 0   ///< Police dispatch
#define SLOT_FIRE   1   ///< Fire department dispatch
#define SLOT_MED    2   ///< Medical/ambulance dispatch
#define SLOT_OTH    3   ///< Other emergency type dispatch
#define MAX_ALARM_SLOTS 4

/**
 * @brief Create new emergency alarm on Noonlight platform
 * 
 * Initiates professional dispatch for triggered zone.
 * Sends to appropriate slot based on zone type (fire, police, medical, other).
 * 
 * @param conf Configuration with Noonlight credentials
 * @param z Zone that triggered (contains type and description)
 * @param slot Emergency type: SLOT_POLICE, SLOT_FIRE, SLOT_MED, or SLOT_OTH
 * 
 * @return true if alarm created successfully, false on HTTP error
 * 
 * @example
 * if (noonlight_create_alarm(storage_get_config(), storage_get_zone(0), SLOT_FIRE)) {
 *     LOG_INFO("Fire alarm dispatched");
 * }
 */
bool noonlight_create_alarm(config_t *conf, zone_t *z, int slot);

/**
 * @brief Log zone trigger event against existing alarm
 * 
 * Records that a specific zone triggered during ongoing alarm.
 * Helps dispatcher understand which sensors detected emergency.
 * 
 * @param conf Configuration with Noonlight credentials
 * @param z Zone that triggered (name, type logged)
 * @param alarm_id Alarm ID from noonlight_create_alarm() return value
 * 
 * @example
 * noonlight_log_event(storage_get_config(), storage_get_zone(2), "alarm_abc123");
 */
void noonlight_log_event(config_t *conf, zone_t *z, const char* alarm_id);

/**
 * @brief Send instructions to emergency responders
 * 
 * Delivers specific instructions to dispatcher (e.g., "disabled occupant in bedroom").
 * Helps first responders prepare for scene.
 * 
 * @param conf Configuration with Noonlight credentials
 * @param alarm_id Alarm ID from noonlight_create_alarm() return value
 * 
 * @example
 * noonlight_send_instructions(storage_get_config(), "alarm_abc123");
 */
void noonlight_send_instructions(config_t *conf, const char* alarm_id);

/**
 * @brief Sync emergency contacts for alarm
 * 
 * Registers system users as emergency contacts.
 * Dispatcher can contact them for additional information.
 * 
 * @param conf Configuration with Noonlight credentials
 * @param users_list Array of user structures
 * @param count Number of users
 * @param alarm_id Alarm ID from noonlight_create_alarm() return value
 * 
 * @example
 * noonlight_sync_people(storage_get_config(), users, user_count, "alarm_abc123");
 */
void noonlight_sync_people(config_t *conf, user_t *users_list, int count, const char* alarm_id);

/**
 * @brief Cancel active alarm on Noonlight platform
 * 
 * Requires dispatcher verification (PIN/code check at Noonlight).
 * Useful if alarm triggered falsely or user already handled emergency.
 * 
 * @param conf Configuration with Noonlight credentials
 * @param id Alarm ID from noonlight_create_alarm() return value
 * 
 * @return true if cancellation successful, false on verification failure or error
 * 
 * @example
 * if (noonlight_cancel_alarm(storage_get_config(), alarm_id)) {
 *     LOG_INFO("Emergency dispatch cancelled");
 * }
 */
bool noonlight_cancel_alarm(config_t *conf, const char *id);

/**
 * @brief Background task for Noonlight synchronization
 * 
 * Periodically syncs alarm state and checks for dispatcher commands.
 * Called by network loop to maintain connection.
 * 
 * @param conf Configuration with Noonlight credentials
 */
void noonlight_sync_task(config_t *conf);

/**
 * @brief Query current alarm status from Noonlight
 * 
 * Checks dispatcher state (pending, dispatching, arrived, cancelled, etc.).
 * Used by web UI and mobile apps to show real-time status.
 * 
 * @param alarm_id Alarm ID from noonlight_create_alarm() return value
 * 
 * @return Status code (0=pending, 1=dispatching, 2=arrived, etc.)
 * 
 * @example
 * int status = os_get_noonlight_status("alarm_abc123");
 * if (status == 2) LOG_INFO("Police arrived on scene");
 */
int os_get_noonlight_status(const char* alarm_id);

#endif