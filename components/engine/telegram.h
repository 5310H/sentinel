#ifndef TELEGRAM_H
#define TELEGRAM_H

/**
 * @file telegram.h
 * @brief Telegram Bot API notifications
 * 
 * Sends alert and cancellation messages via Telegram Bot API.
 * Uses non-blocking esp_http_client for asynchronous delivery.
 * 
 * Features:
 * - HTTPS with TLS certificate verification
 * - Asynchronous message delivery (non-blocking)
 * - Alert messages: Zone name, type, timestamp
 * - Cancellation messages: System disarmed confirmation
 * - Error logging on HTTP failures
 * 
 * Note: Requires telegram_bot_token and telegram_chat_id in config.json
 */

#include "storage_mgr.h"

/**
 * @brief Send zone alarm notification via Telegram
 * 
 * Constructs alert message with:
 * - Zone name from z->name
 * - Zone type (Door/Motion/Fire/etc.) from z->type
 * - Triggered timestamp
 * - System arm mode
 * 
 * Uses non-blocking HTTP POST to api.telegram.org/botXXX/sendMessage.
 * Response handled asynchronously by event callback (logs status only).
 * Never blocks the calling task.
 * 
 * @param conf Configuration with telegram_bot_token and telegram_chat_id
 * @param z Zone that triggered the alarm
 * 
 * @example
 * telegram_send_alert(&config, &zones[0]);  // Alert for first zone
 */
void telegram_send_alert(config_t *conf, zone_t *z);

/**
 * @brief Send system disarm confirmation via Telegram
 * 
 * Sends cancellation/reset message indicating:
 * - Alarm has been disarmed
 * - Time of disarm
 * - User/action that disarmed (if available)
 * 
 * Uses non-blocking HTTP POST like send_alert().
 * Called when system is disarmed after triggering alarm.
 * 
 * @param conf Configuration with telegram_bot_token and telegram_chat_id
 * 
 * @example
 * telegram_send_cancellation(&config);
 */
void telegram_send_cancellation(config_t *conf);

#endif