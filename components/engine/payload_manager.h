#ifndef PAYLOAD_MANAGER_H
#define PAYLOAD_MANAGER_H

/**
 * @file payload_manager.h
 * @brief JSON payload construction for multi-channel alerting
 * 
 * Builds service-specific JSON payloads for:
 * - Noonlight professional monitoring
 * - Telegram Bot API
 * - SMTP email servers
 * 
 * Provides unified interface for formatting alarm data
 * according to each service's API requirements.
 * 
 * Features:
 * - Service-specific JSON schema handling
 * - Response parsing for alarm IDs
 * - Reusable payload buffer (thread-safe single instance)
 */

#include <stdbool.h>
#include "config.h"
#include "storage_mgr.h"

// Notification channel slots
#define SLOT_NOONLIGHT 0  ///< Professional monitoring dispatch
#define SLOT_TELEGRAM  1  ///< Telegram Bot API
#define SLOT_SMTP      2  ///< Email SMTP service

/**
 * @brief Shared payload buffer for all formatters
 * 
 * Single 1KB buffer reused by all payload builders.
 * Thread-safe: mutex or serialization required if called from multiple tasks.
 * Updated by each payload_build_* function.
 * 
 * Used to minimize stack usage on embedded systems.
 */
extern char payload_buffer[1024];

/**
 * @brief Build Noonlight API alarm JSON payload
 * 
 * Constructs JSON for noonlight_create_alarm() with:
 * - Zone name, type, description
 * - Location (from config)
 * - Is_update flag for event logging
 * - Alarm callback URL
 * 
 * @param o Configuration with credentials and metadata
 * @param z Zone that triggered
 * @param is_update true for event updates, false for new alarm
 * 
 * @example
 * payload_build_noonlight(&config, &zones[0], false);
 * // payload_buffer now contains Noonlight JSON
 */
void payload_build_noonlight(config_t *o, zone_t *z, bool is_update);

/**
 * @brief Build Telegram Bot API message JSON payload
 * 
 * Constructs JSON for telegram/sendMessage with:
 * - Zone name and type
 * - Arm mode and timestamp
 * - HTML formatted message text
 * - Chat ID from config
 * 
 * @param o Configuration with telegram_chat_id and credentials
 * @param z Zone that triggered
 * 
 * @example
 * payload_build_telegram(&config, &zones[2]);
 * // payload_buffer now contains Telegram JSON message
 */
void payload_build_telegram(config_t *o, zone_t *z);

/**
 * @brief Build SMTP email payload JSON
 * 
 * Constructs JSON for email recipient with:
 * - Zone name, type, description
 * - Recipient email address
 * - Alert timestamp
 * - System location
 * - HTML email template
 * 
 * @param o Configuration with SMTP server and sender email
 * @param z Zone that triggered
 * @param c User recipient (name, email)
 * 
 * @example
 * payload_build_smtp(&config, &zones[1], &users[0]);
 * // payload_buffer now contains SMTP command JSON
 */
void payload_build_smtp(config_t *o, zone_t *z, user_t *c);

/**
 * @brief Parse service response for alarm ID
 * 
 * Extracts unique alarm identifier from HTTP response.
 * Different parsing logic per service:
 * - Noonlight: JSON field "alarm_id"
 * - Telegram: JSON field "result.message_id"
 * - SMTP: HTTP status code becomes ID
 * 
 * @param slot Notification channel: SLOT_NOONLIGHT, SLOT_TELEGRAM, or SLOT_SMTP
 * @param raw HTTP response body (raw JSON/text)
 * @param out_id Output buffer for parsed ID
 * 
 * @return true if ID parsed successfully, false on parse error
 * 
 * @example
 * char alarm_id[64];
 * if (payload_parse_response(SLOT_NOONLIGHT, response_body, alarm_id)) {
 *     LOG_INFO("Alarm ID: %s", alarm_id);
 * }
 */
bool payload_parse_response(int slot, const char *raw, char *out_id);

#endif