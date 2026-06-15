#ifndef HAL_TIME_H
#define HAL_TIME_H

/**
 * @file hal_time.h
 * @brief Time management and formatting
 * 
 * Provides:
 * - NTP/SNTP time synchronization
 * - Formatted timestamp generation
 * - Time zone support
 * - Platform-specific implementations (ESP32/Linux)
 * 
 * Used for:
 * - Alarm event timestamps
 * - Log file timestamps
 * - MQTT heartbeat timing
 * - NVS state persistence metadata
 */

#include <stdsize.h>

/**
 * @brief Initialize time synchronization subsystem
 * 
 * Sets up NTP/SNTP client for:
 * - ESP32: Configures SNTP from lwIP stack
 * - Linux: Uses system time
 * 
 * Synchronizes with NTP servers defined in config.
 * Called once during app_main() initialization.
 * 
 * @example
 * hal_time_init();  // System clock now synced to internet time
 */
void hal_time_init(void);

/**
 * @brief Get current time as formatted string
 * 
 * Returns timestamp in format: "2024-01-15 14:30:45"
 * Useful for:
 * - Alarm event logging
 * - Telegram/email alert timestamps
 * - Noonlight incident creation
 * - Web UI display
 * 
 * Thread-safe: each call uses own thread-local buffer.
 * 
 * @param buffer Output character buffer
 * @param len Buffer size (recommended: 64+ bytes)
 * 
 * @example
 * char timestamp[64];
 * get_sentinel_time(timestamp, sizeof(timestamp));
 * LOG_INFO("Alarm triggered at %s", timestamp);
 */
void get_sentinel_time(char *buffer, size_t len);

#endif