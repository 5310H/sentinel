/**
 * @file system_monitoring.h
 * @brief System monitoring and diagnostics for KC868-A8V3 hardware
 * 
 * Provides comprehensive system monitoring including:
 * - Tamper detection (case open sensor on DI7)
 * - Power loss detection (UPS signal on DI8)
 * - System temperature via DS3231 RTC
 * - Event logging to 24C02 EEPROM (ring buffer)
 * - RTC time synchronization
 * 
 * @author Sentinel-ESP Team
 * @version 1.0.0
 * @date 2026-01-21
 */

#ifndef SYSTEM_MONITORING_H
#define SYSTEM_MONITORING_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event types for system logging
 */
typedef enum {
    EVENT_TYPE_SYSTEM_START = 0x01,      /**< System startup */
    EVENT_TYPE_ARM = 0x02,               /**< System armed */
    EVENT_TYPE_DISARM = 0x03,            /**< System disarmed */
    EVENT_TYPE_ZONE_TRIGGER = 0x04,      /**< Zone trigger */
    EVENT_TYPE_TAMPER = 0x05,            /**< Tamper detected */
    EVENT_TYPE_POWER_LOSS = 0x06,        /**< Power loss detected */
    EVENT_TYPE_POWER_RESTORE = 0x07,     /**< Power restored */
    EVENT_TYPE_TEMPERATURE_HIGH = 0x08,  /**< High temperature */
    EVENT_TYPE_NETWORK_LOST = 0x09,      /**< Network lost */
    EVENT_TYPE_NETWORK_FOUND = 0x0A,     /**< Network found */
    EVENT_TYPE_AUTH_FAIL = 0x0B,         /**< Authentication failed */
    EVENT_TYPE_ALERT_SENT = 0x0C,        /**< Alert sent to service */
} system_event_type_t;

/**
 * @brief System status structure containing all monitored values
 */
typedef struct {
    bool tamper_detected;                /**< Case open sensor status */
    bool power_loss_detected;            /**< UPS signal status (true = on battery) */
    bool on_backup_power;                /**< System running on UPS */
    uint32_t backup_power_duration_ms;   /**< Milliseconds since power loss */
    int8_t temperature_c;                /**< System temperature in Celsius */
    uint16_t temperature_raw;            /**< Raw temperature value from DS3231 */
    uint32_t uptime_seconds;             /**< System uptime since last boot */
    uint32_t last_event_timestamp;       /**< Timestamp of last logged event */
    uint8_t event_count;                 /**< Number of events in EEPROM buffer */
    bool rtc_synchronized;               /**< RTC synced with NTP or set manually */
    uint32_t rtc_battery_ok;             /**< RTC battery status (from I2C) */
} system_status_t;

/**
 * @brief Event log entry (8 bytes total for EEPROM storage)
 */
typedef struct {
    uint32_t timestamp;                  /**< Event timestamp (4 bytes) */
    uint8_t event_type;                  /**< Event type from system_event_type_t (1 byte) */
    uint8_t zone_id;                     /**< Zone ID if applicable (1 byte) */
    uint8_t reserved[2];                 /**< Reserved for future use (2 bytes) */
} __attribute__((packed)) event_log_entry_t;

/**
 * @brief Initialize system monitoring
 * 
 * Sets up I2C communication with RTC and I/O expander,
 * initializes EEPROM event ring buffer, and starts monitoring
 * the tamper and power loss inputs.
 * 
 * @return ESP_OK on success, error code otherwise
 * 
 * @example
 * ```c
 * if (system_monitoring_init(bus_handle) == ESP_OK) {
 *     ESP_LOGI(TAG, "System monitoring initialized");
 * }
 * ```
 */
int system_monitoring_init(void* i2c_bus_handle);

/**
 * @brief Update system monitoring status
 * 
 * Reads current sensor values including:
 * - DS3231 temperature
 * - PCF8575 input states (DI7=tamper, DI8=power loss)
 * - UPS backup power duration
 * - Event counter from EEPROM
 * 
 * Should be called periodically (recommended: every 1-5 seconds)
 * 
 * @param[out] status Pointer to system_status_t to fill with current values
 * @return ESP_OK on success, error code otherwise
 * 
 * @example
 * ```c
 * system_status_t status = {0};
 * if (system_monitoring_update(&status) == ESP_OK) {
 *     printf("Temp: %d°C, Tamper: %s\n", status.temperature_c,
 *            status.tamper_detected ? "YES" : "NO");
 * }
 * ```
 */
int system_monitoring_update(system_status_t *status);

/**
 * @brief Get current system status
 * 
 * Returns the most recently updated system status from the last
 * system_monitoring_update() call.
 * 
 * @param[out] status Pointer to system_status_t to fill
 * @return ESP_OK on success
 */
int system_monitoring_get_status(system_status_t *status);

/**
 * @brief Log an event to EEPROM ring buffer
 * 
 * Stores event to 24C02 EEPROM (256 bytes total, 32 events of 8 bytes).
 * Oldest events are automatically overwritten when buffer is full.
 * 
 * @param event_type Type of event from system_event_type_t
 * @param zone_id Zone ID (0 if not applicable)
 * @return ESP_OK on success, error code otherwise
 * 
 * @example
 * ```c
 * system_monitoring_log_event(EVENT_TYPE_TAMPER, 0);
 * ```
 */
int system_monitoring_log_event(system_event_type_t event_type, uint8_t zone_id);

/**
 * @brief Get event from EEPROM by index
 * 
 * Retrieves event from ring buffer. Index 0 is the oldest event,
 * higher indices are more recent.
 * 
 * @param index Event index (0 to max_events-1)
 * @param[out] entry Pointer to event_log_entry_t to fill
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_get_event(uint8_t index, event_log_entry_t *entry);

/**
 * @brief Get all events from EEPROM
 * 
 * Retrieves all stored events in chronological order.
 * 
 * @param[out] entries Array of event_log_entry_t (must hold at least 32 entries)
 * @param[out] count Pointer to store number of entries retrieved
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_get_all_events(event_log_entry_t *entries, uint8_t *count);

/**
 * @brief Clear all events from EEPROM
 * 
 * Erases the event ring buffer and resets event counter.
 * 
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_clear_events(void);

/**
 * @brief Synchronize RTC with NTP time
 * 
 * Updates DS3231 RTC with current time from NTP server.
 * Falls back to system time if NTP unavailable.
 * 
 * @param ntp_server NTP server address (e.g., "pool.ntp.org")
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_sync_rtc(const char *ntp_server);

/**
 * @brief Set RTC time manually
 * 
 * Updates DS3231 RTC with provided timestamp.
 * Useful for systems without NTP access.
 * 
 * @param timestamp Unix timestamp (seconds since epoch)
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_set_rtc_time(uint32_t timestamp);

/**
 * @brief Get current time from RTC
 * 
 * Retrieves current time from DS3231 RTC (battery-backed) with BCD decoding.
 * Falls back to system time if RTC read fails.
 * 
 * @return Unix timestamp (seconds since epoch), or current system time on error
 */
uint32_t system_monitoring_get_rtc_time(void);

/**
 * @brief Check RTC oscillator status
 * 
 * Verifies DS3231 oscillator is running and clears OSF flag if stopped.
 * Should be called on boot to detect power loss events.
 * 
 * @return ESP_OK if oscillator running, ESP_FAIL if was stopped, error code on I2C failure
 */
int system_monitoring_check_rtc_status(void);

/**
 * @brief Read temperature from DS3231
 * 
 * Reads temperature sensor in RTC chip. Provides microsecond-precision
 * timestamps for event logging and system diagnostics.
 * 
 * @param[out] temp_c Pointer to store temperature in Celsius
 * @param[out] temp_raw Pointer to store raw 14-bit temperature value
 * @return ESP_OK on success, error code otherwise
 */
int system_monitoring_read_temperature(int8_t *temp_c, uint16_t *temp_raw);

/**
 * @brief Get formatted system diagnostics JSON
 * 
 * Generates JSON string with current system status for web UI display.
 * String should be freed by caller with free().
 * 
 * @param[out] json_str Pointer to store allocated JSON string
 * @return ESP_OK on success, error code otherwise
 * 
 * @example
 * ```c
 * char *diag_json = NULL;
 * if (system_monitoring_get_diagnostics_json(&diag_json) == ESP_OK) {
 *     printf("Diagnostics: %s\n", diag_json);
 *     free(diag_json);
 * }
 * ```
 */
int system_monitoring_get_diagnostics_json(char **json_str);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_MONITORING_H
