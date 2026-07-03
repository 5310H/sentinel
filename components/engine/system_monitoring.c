/**
 * @file system_monitoring.c
 * @brief System monitoring implementation for KC868-A8V3 hardware
 */

#include "system_monitoring.h"
#include "logging.h"
#include "hardware_config.h"
#include "network_watchdog.h"
#include <string.h>
#include <time.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#else
// Linux mock implementation
#include <stdlib.h>
#include <stdio.h>
#include <cjson/cJSON.h>

#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_LOGD(tag, fmt, ...) printf("[%s] [D] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[%s] [I] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[%s] [W] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[%s] [E] " fmt "\n", tag, ##__VA_ARGS__)

typedef void* SemaphoreHandle_t;
typedef void* i2c_master_bus_handle_t;
typedef void* i2c_master_dev_handle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return malloc(1); }
void vSemaphoreDelete(SemaphoreHandle_t h) { free(h); }
int xSemaphoreTake(SemaphoreHandle_t h, int t) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t h) { return 1; }
#endif

static const char *TAG = "SYSTEM_MON";

// I2C addresses
#define RTC_ADDR 0x68              // DS3231
#define EEPROM_ADDR 0x50           // 24C02
#define IO_EXPANDER_ADDR 0x20      // PCF8575

// PCF8575 input bits
#define DI7_BIT 6                  // Tamper (case open)
#define DI8_BIT 7                  // Power loss (UPS signal)

// EEPROM layout
#define EEPROM_SIZE 256
#define EVENT_SIZE 8
#define MAX_EVENTS (EEPROM_SIZE / EVENT_SIZE)
#define EVENT_COUNTER_ADDR 0       // 1 byte: current event count

// RTC registers
#define RTC_TEMP_MSB 0x11
#define RTC_TEMP_LSB 0x12
#define RTC_STATUS_REG 0x0F
#define RTC_OSF_BIT 0x80           // Oscillator Stop Flag

// BCD conversion macros for DS3231
#define DEC2BCD(val) (((val / 10) << 4) | (val % 10))
#define BCD2DEC(val) (((val >> 4) * 10) + (val & 0x0F))

// Local state
static system_status_t g_current_status = {0};
static SemaphoreHandle_t g_monitoring_mutex = NULL;

#ifdef ESP_PLATFORM
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t rtc_dev = NULL;
static i2c_master_dev_handle_t eeprom_dev = NULL;
static i2c_master_dev_handle_t io_exp_dev = NULL;
#endif

/**
 * @brief Initialize I2C peripherals
 * 
 * Sets up I2C0 bus for RTC, EEPROM, and I/O expander communication.
 */
static int _i2c_init(void* bus_handle_param) {
#ifdef ESP_PLATFORM
    // Use the shared I2C bus handle passed from main
    i2c_bus_handle = (i2c_master_bus_handle_t)bus_handle_param;
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add RTC device to existing bus
    i2c_device_config_t rtc_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_ADDR,
        .scl_speed_hz = 100000,
    };
    
    if (i2c_master_bus_add_device(i2c_bus_handle, &rtc_config, &rtc_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RTC device to I2C bus");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add EEPROM device
    i2c_device_config_t eeprom_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EEPROM_ADDR,
        .scl_speed_hz = 100000,
    };
    
    if (i2c_master_bus_add_device(i2c_bus_handle, &eeprom_config, &eeprom_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add EEPROM device to I2C bus");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add I/O Expander device
    i2c_device_config_t io_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IO_EXPANDER_ADDR,
        .scl_speed_hz = 100000,
    };
    
    if (i2c_master_bus_add_device(i2c_bus_handle, &io_config, &io_exp_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I/O expander to I2C bus");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

/**
 * @brief Read temperature from DS3231 RTC
 */
static int _read_rtc_temperature(int8_t *temp_c, uint16_t *temp_raw) {
    if (!temp_c || !temp_raw) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (!rtc_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t buffer[2] = {0};
    
    // Read temperature registers (MSB at 0x11, LSB at 0x12)
    i2c_master_transmit_receive(rtc_dev, (uint8_t[]){RTC_TEMP_MSB}, 1, buffer, 2, -1);
    
    // MSB is integer temp, LSB upper 2 bits are fractional
    int8_t temp = (int8_t)buffer[0];
    
    *temp_c = temp;
    *temp_raw = ((uint16_t)buffer[0] << 8) | buffer[1];
#else
    // Linux mock: return realistic temperature
    *temp_c = 28;
    *temp_raw = 0x1C00;  // 28°C
#endif

    return ESP_OK;
}

/**
 * @brief Read IO expander input status
 */
static int _read_io_expander(uint16_t *input_status) {
    if (!input_status) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (!io_exp_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t buffer[2] = {0};
    i2c_master_receive(io_exp_dev, buffer, 2, -1);
    
    *input_status = (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
#else
    // Linux mock: no tamper, not on battery
    *input_status = 0xFFFF;  // All inputs high (normal state)
#endif

    return ESP_OK;
}

/**
 * @brief Write event to EEPROM ring buffer
 */
static int _write_eeprom_event(const event_log_entry_t *entry) {
    if (!entry) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (!eeprom_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Read current event count
    uint8_t count_buf[1] = {0};
    i2c_master_transmit_receive(eeprom_dev, (uint8_t[]){EVENT_COUNTER_ADDR}, 1, count_buf, 1, -1);
    
    uint8_t event_count = count_buf[0];
    
    // Calculate write offset (after counter byte)
    uint8_t write_offset = 1 + ((event_count % MAX_EVENTS) * EVENT_SIZE);
    
    // Prepare write buffer: address + event data
    uint8_t write_buf[1 + EVENT_SIZE];
    write_buf[0] = write_offset;
    memcpy(&write_buf[1], (uint8_t *)entry, EVENT_SIZE);
    
    i2c_master_transmit(eeprom_dev, write_buf, sizeof(write_buf), -1);
    
    // Update event counter (wrap around at MAX_EVENTS)
    event_count = (event_count + 1) % 256;  // Store actual count, not wrapped
    uint8_t counter_buf[2] = {EVENT_COUNTER_ADDR, event_count};
    i2c_master_transmit(eeprom_dev, counter_buf, 2, -1);
    
    vTaskDelay(10 / portTICK_PERIOD_MS);  // EEPROM write delay
#else
    // Linux mock: just log the event
    ESP_LOGI(TAG, "Mock event logged: type=%d, zone=%d, time=%u",
             entry->event_type, entry->zone_id, entry->timestamp);
#endif

    return ESP_OK;
}

/**
 * @brief Read event from EEPROM
 */
static int _read_eeprom_event(uint8_t index, event_log_entry_t *entry) {
    if (!entry || index >= MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (!eeprom_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t read_offset = 1 + (index * EVENT_SIZE);
    uint8_t buffer[EVENT_SIZE];
    
    i2c_master_transmit_receive(eeprom_dev, &read_offset, 1, buffer, EVENT_SIZE, -1);
    memcpy(entry, buffer, EVENT_SIZE);
#else
    // Linux mock
    memset(entry, 0, sizeof(*entry));
#endif

    return ESP_OK;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int system_monitoring_init(void* i2c_bus_handle) {
    if (g_monitoring_mutex == NULL) {
        g_monitoring_mutex = xSemaphoreCreateMutex();
        if (g_monitoring_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create monitoring mutex");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    if (_i2c_init(i2c_bus_handle) != ESP_OK) {
        ESP_LOGW(TAG, "I2C initialization failed (may be unavailable in test mode)");
    }
    
    // Initialize status
    g_current_status.rtc_battery_ok = 1;
    g_current_status.rtc_synchronized = 0;
    g_current_status.temperature_c = 0;
    
    ESP_LOGI(TAG, "System monitoring initialized");
    return ESP_OK;
}

int system_monitoring_update(system_status_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!xSemaphoreTake(g_monitoring_mutex, 1000)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read temperature
    _read_rtc_temperature(&g_current_status.temperature_c, &g_current_status.temperature_raw);
    
    // Read IO expander status
    uint16_t io_status = 0;
    _read_io_expander(&io_status);
    
    // Extract tamper and power loss bits
    bool tamper_now = !(io_status & (1 << DI7_BIT));
    bool power_loss_now = !(io_status & (1 << DI8_BIT));
    
    // Detect state changes
    if (tamper_now && !g_current_status.tamper_detected) {
        ESP_LOGW(TAG, "TAMPER DETECTED - Case opened!");
        system_monitoring_log_event(EVENT_TYPE_TAMPER, 0);
    }
    
    if (power_loss_now && !g_current_status.power_loss_detected) {
        ESP_LOGW(TAG, "POWER LOSS - Switched to backup power");
        system_monitoring_log_event(EVENT_TYPE_POWER_LOSS, 0);
        g_current_status.backup_power_duration_ms = 0;
    } else if (!power_loss_now && g_current_status.power_loss_detected) {
        ESP_LOGI(TAG, "POWER RESTORED after %" PRIu32 " ms on backup", 
                 g_current_status.backup_power_duration_ms);
        system_monitoring_log_event(EVENT_TYPE_POWER_RESTORE, 0);
    }
    
    g_current_status.tamper_detected = tamper_now;
    g_current_status.power_loss_detected = power_loss_now;
    
    if (power_loss_now) {
        g_current_status.backup_power_duration_ms += 1000;  // Approximate increment
        g_current_status.on_backup_power = true;
        
        // Alert after 30 minutes on backup
        if (g_current_status.backup_power_duration_ms > (30 * 60 * 1000)) {
            ESP_LOGW(TAG, "Extended backup power: %" PRIu32 " minutes",
                     g_current_status.backup_power_duration_ms / (60 * 1000));
        }
    } else {
        g_current_status.on_backup_power = false;
        g_current_status.backup_power_duration_ms = 0;
    }
    
    // Alert on high temperature
    if (g_current_status.temperature_c > 45) {
        ESP_LOGW(TAG, "HIGH TEMPERATURE: %d°C", g_current_status.temperature_c);
        system_monitoring_log_event(EVENT_TYPE_TEMPERATURE_HIGH, 0);
    }
    
    g_current_status.last_event_timestamp = time(NULL);
    
#ifdef ESP_PLATFORM
    g_current_status.uptime_seconds = esp_timer_get_time() / 1000000;
#else
    static time_t start_t = 0;
    if(start_t == 0) start_t = time(NULL);
    g_current_status.uptime_seconds = (uint32_t)(time(NULL) - start_t);
#endif
    
    memcpy(status, &g_current_status, sizeof(system_status_t));
    xSemaphoreGive(g_monitoring_mutex);
    
    return ESP_OK;
}

int system_monitoring_get_status(system_status_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!xSemaphoreTake(g_monitoring_mutex, 1000)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(status, &g_current_status, sizeof(system_status_t));
    xSemaphoreGive(g_monitoring_mutex);
    
    return ESP_OK;
}

int system_monitoring_log_event(system_event_type_t event_type, uint8_t zone_id) {
    event_log_entry_t entry = {
        .timestamp = time(NULL),
        .event_type = (uint8_t)event_type,
        .zone_id = zone_id,
    };
    
    return _write_eeprom_event(&entry);
}

int system_monitoring_get_event(uint8_t index, event_log_entry_t *entry) {
    return _read_eeprom_event(index, entry);
}

int system_monitoring_get_all_events(event_log_entry_t *entries, uint8_t *count) {
    if (!entries || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *count = g_current_status.event_count;
    for (int i = 0; i < *count && i < MAX_EVENTS; i++) {
        _read_eeprom_event(i, &entries[i]);
    }
    
    return ESP_OK;
}

int system_monitoring_clear_events(void) {
#ifdef ESP_PLATFORM
    if (!eeprom_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Clear counter
    uint8_t counter_buf[2] = {EVENT_COUNTER_ADDR, 0};
    i2c_master_transmit(eeprom_dev, counter_buf, 2, -1);
#endif
    
    g_current_status.event_count = 0;
    return ESP_OK;
}

int system_monitoring_sync_rtc(const char *ntp_server) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Attempting RTC sync with NTP server: %s", ntp_server);
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    // Wait for time to be set (up to 10 seconds)
    int retry = 0;
    const int retry_count = 10;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (retry == retry_count) {
        ESP_LOGW(TAG, "SNTP sync failed");
        return ESP_FAIL;
    }

    // Update RTC with new system time
    time_t now = time(NULL);
    system_monitoring_set_rtc_time((uint32_t)now);
#endif

    if (!xSemaphoreTake(g_monitoring_mutex, 1000)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_current_status.rtc_synchronized = 1;
    xSemaphoreGive(g_monitoring_mutex);
    
    return ESP_OK;
}

int system_monitoring_set_rtc_time(uint32_t timestamp) {
#ifdef ESP_PLATFORM
    if (!rtc_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Convert timestamp to time structure
    time_t t = timestamp;
    struct tm *tm_info = gmtime(&t);
    
    // DS3231 requires BCD encoding
    uint8_t write_buf[8] = {
        0x00,  // Starting register address
        DEC2BCD(tm_info->tm_sec),               // Seconds (0x00)
        DEC2BCD(tm_info->tm_min),               // Minutes (0x01)
        DEC2BCD(tm_info->tm_hour),              // Hours 24h format (0x02)
        DEC2BCD(tm_info->tm_wday + 1),          // Day of week 1-7 (0x03)
        DEC2BCD(tm_info->tm_mday),              // Date 1-31 (0x04)
        DEC2BCD(tm_info->tm_mon + 1),           // Month 1-12 (0x05)
        DEC2BCD(tm_info->tm_year % 100),        // Year 00-99 (0x06)
    };
    
    esp_err_t ret = i2c_master_transmit(rtc_dev, write_buf, sizeof(write_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write RTC time: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "RTC time set: %04d-%02d-%02d %02d:%02d:%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
#endif
    
    if (!xSemaphoreTake(g_monitoring_mutex, 1000)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_current_status.rtc_synchronized = 1;
    xSemaphoreGive(g_monitoring_mutex);
    
    return ESP_OK;
}

uint32_t system_monitoring_get_rtc_time(void) {
#ifdef ESP_PLATFORM
    if (!rtc_dev) {
        ESP_LOGW(TAG, "RTC device not initialized, using system time");
        return time(NULL);
    }
    
    uint8_t buffer[7] = {0};
    
    // Read time registers 0x00-0x06
    esp_err_t ret = i2c_master_transmit_receive(rtc_dev, (uint8_t[]){0x00}, 1, buffer, 7, -1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read RTC time: %s", esp_err_to_name(ret));
        return time(NULL);
    }
    
    // Convert BCD to decimal
    struct tm tm_info = {0};
    tm_info.tm_sec = BCD2DEC(buffer[0] & 0x7F);      // Mask CH bit
    tm_info.tm_min = BCD2DEC(buffer[1] & 0x7F);
    tm_info.tm_hour = BCD2DEC(buffer[2] & 0x3F);     // 24-hour format
    // buffer[3] is day of week (not needed for timestamp)
    tm_info.tm_mday = BCD2DEC(buffer[4] & 0x3F);
    tm_info.tm_mon = BCD2DEC(buffer[5] & 0x1F) - 1;  // Month 0-11
    tm_info.tm_year = BCD2DEC(buffer[6]) + 100;      // Years since 1900
    tm_info.tm_isdst = -1;                           // Auto-determine DST
    
    time_t rtc_time = mktime(&tm_info);
    if (rtc_time == -1) {
        ESP_LOGW(TAG, "Failed to convert RTC time to timestamp");
        return time(NULL);
    }
    
    return (uint32_t)rtc_time;
#else
    return time(NULL);
#endif
}

int system_monitoring_check_rtc_status(void) {
#ifdef ESP_PLATFORM
    if (!rtc_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t status_reg = 0;
    
    // Read status register 0x0F
    esp_err_t ret = i2c_master_transmit_receive(rtc_dev, (uint8_t[]){RTC_STATUS_REG}, 1, &status_reg, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC status: %s", esp_err_to_name(ret));
        return ret;
    }
    
    bool oscillator_stopped = (status_reg & RTC_OSF_BIT) != 0;
    
    if (oscillator_stopped) {
        ESP_LOGW(TAG, "RTC oscillator was stopped - time may be incorrect!");
        
        // Clear OSF flag
        status_reg &= ~RTC_OSF_BIT;
        uint8_t clear_buf[2] = {RTC_STATUS_REG, status_reg};
        i2c_master_transmit(rtc_dev, clear_buf, 2, -1);
        
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "RTC oscillator running normally");
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

int system_monitoring_read_temperature(int8_t *temp_c, uint16_t *temp_raw) {
    return _read_rtc_temperature(temp_c, temp_raw);
}

int system_monitoring_get_diagnostics_json(char **json_str) {
    if (!json_str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    system_status_t status = {0};
    if (system_monitoring_get_status(&status) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON_AddNumberToObject(root, "temperature_c", status.temperature_c);
    cJSON_AddNumberToObject(root, "temperature_raw", status.temperature_raw);
    cJSON_AddBoolToObject(root, "tamper_detected", status.tamper_detected);
    cJSON_AddBoolToObject(root, "power_loss_detected", status.power_loss_detected);
    cJSON_AddBoolToObject(root, "on_backup_power", status.on_backup_power);
    cJSON_AddNumberToObject(root, "backup_power_duration_ms", status.backup_power_duration_ms);
    cJSON_AddNumberToObject(root, "uptime_seconds", status.uptime_seconds);
    cJSON_AddNumberToObject(root, "event_count", status.event_count);
    cJSON_AddBoolToObject(root, "rtc_synchronized", status.rtc_synchronized);
    cJSON_AddNumberToObject(root, "rtc_battery_ok", status.rtc_battery_ok);
    cJSON_AddNumberToObject(root, "network_state", network_watchdog_get_state());
    
    *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    return ESP_OK;
}
