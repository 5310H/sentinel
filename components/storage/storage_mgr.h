#ifndef STORAGE_MGR_H
#define STORAGE_MGR_H

/**
 * @file storage_mgr.h
 * @brief Configuration storage and persistence management
 *
 * Manages loading/saving of alarm system configuration from JSON files:
 * - User accounts and PINs
 * - Zone definitions (sensors, doors, windows, etc.)
 * - Relay assignments (alarm outputs, lights, locks)
 * - Network configuration
 * - Professional monitoring service credentials
 * - Notification preferences (Email, Telegram, Noonlight)
 */

#include "../security/session_mgr.h"

// Forward declaration to avoid pulling in full esphome_api.h everywhere
typedef struct esph_session esph_session_t;

// ESP-IDF compatibility for Linux builds
#ifndef ESP_PLATFORM
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
#endif

#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
typedef esp_err_t status_t;
#else
typedef int status_t;
#endif

// 1. Constants
#define STR_SMALL 64
#define STR_MEDIUM 128
#define STR_LARGE 512
#define MAX_ZONES 32 // Reduced from 96 to save RAM - only need physical zones
#define MAX_USERS 10
#define MAX_RELAYS 16 // Reduced from 64 to save RAM
#define MAX_ALARM_SLOTS 4

typedef struct {
  bool use_dhcp;
  char ip[STR_SMALL];
  char gateway[STR_SMALL];
  char netmask[STR_SMALL];
  char dns[STR_SMALL];
} network_t;

network_t *storage_get_network(void);

/**
 * @brief Load network configuration from storage
 *
 * Reads network.json and configures DHCP or static IP settings.
 */
void storage_load_network(void);

// --- Zone Type Enums ---
typedef enum {
  ZONE_TYPE_UNKNOWN = 0,
  ZONE_TYPE_DOOR,
  ZONE_TYPE_WINDOW,
  ZONE_TYPE_MOTION,
  ZONE_TYPE_CAMERA,
  ZONE_TYPE_GLASS_BREAK,
  ZONE_TYPE_FIRE,
  ZONE_TYPE_SMOKE,
  ZONE_TYPE_PANIC,
  ZONE_TYPE_MEDICAL,
  ZONE_TYPE_TEMP,
  ZONE_TYPE_WATER
} zone_type_t;

// --- Zone Type Enums ---
typedef enum {
  ZONE_CALL_UNKNOWN = 0,
  ZONE_CALL_FIRE,
  ZONE_CALL_POLICE,
  ZONE_CALL_MEDICAL,
  ZONE_CALL_OTHER
} zone_call_t;

// --- Relay Type Enums ---
typedef enum {
  RELAY_TYPE_UNKNOWN = 0,
  RELAY_TYPE_ALARM,
  RELAY_TYPE_SIREN,
  RELAY_TYPE_STROBE,
  RELAY_TYPE_CHIME,
  RELAY_TYPE_BELL,
  RELAY_TYPE_LIGHT,
  RELAY_TYPE_DOOR_LOCK,
  RELAY_TYPE_GATE
} relay_type_t;

// Helper functions to convert strings to enums
zone_type_t zone_type_from_string(const char *type_str);
const char *zone_type_to_string(zone_type_t type);
zone_call_t zone_call_from_string(const char *call_str);
const char *zone_call_to_string(zone_call_t call);
relay_type_t relay_type_from_string(const char *type_str);
const char *relay_type_to_string(relay_type_t type);

typedef enum {
  ARMSTATE_DISARMED = 0,
  ARMSTATE_STAY,
  ARMSTATE_AWAY,
  ARMSTATE_NIGHT,
  ARMSTATE_VACATION
} ArmState_t;

typedef enum {
  ALARM_IDLE = 0,
  ALARM_EXITING,
  ALARM_PENDING,
  ALARM_TRIGGERED,
  ALARM_CANCELLED,
  ALARM_TROUBLE
} AlarmStatus_t;

// 2. Struct Definitions
typedef struct {
  char name[STR_SMALL];
  char pin[STR_SMALL];
  char emergency_pin[STR_SMALL]; // Plaintext PIN for emergency services
                                 // (Noonlight)
  char phone[STR_SMALL];
  char email[STR_MEDIUM];
  int notify;
  bool is_admin;
  bool requires_2fa;
  char totp_secret[STR_MEDIUM]; // Base32-encoded TOTP secret (e.g.,
                                // "JBSWY3DPEHPK3PXP")
  char last_otp[STR_SMALL];
} user_t;

typedef struct {
  int id;
  char name[STR_MEDIUM];
  char description[STR_MEDIUM];
  int duration;
  char location[STR_MEDIUM];
  char type[STR_SMALL];
  bool is_repeat;
  int gpio;
} relay_t;

typedef struct {
  int id;
  char name[STR_SMALL];
  char description[STR_MEDIUM];
  char type[STR_SMALL];
  char call[STR_SMALL];
  char location[STR_SMALL];
  char model[STR_SMALL];
  char manufacturer[STR_SMALL];
  bool is_chime;
  bool is_alarm_on_armed_only;
  int gpio;
  bool is_i2c;
  int i2c_address;
  bool is_perimeter;
  bool is_interior;
  bool is_panic;
  bool is_alert_sent;
} zone_t;

typedef struct {
  char hostname[STR_SMALL];        // IP address or hostname
  uint16_t port;                   // API port (default 6053)
  char friendly_name[STR_SMALL];   // Display name
  char description[STR_MEDIUM];    // Zone/relay description
  char location[STR_MEDIUM];       // Zone/relay location
  char password[STR_SMALL];        // API password (optional)
  char encryption_key[STR_MEDIUM]; // Encryption key (optional)
  int8_t virtual_zone_start;       // First virtual zone ID (e.g., 33)
  int8_t virtual_relay_start;      // First virtual relay ID (e.g., 8)
  uint32_t entity_key;             // Entity key for switch/light control
  bool enabled;                    // Enable/disable device
  // Runtime state (not persisted)
  bool is_connected;        // Connection status
  bool use_encryption;      // Using encrypted connection
  esph_session_t *session;  // ESPHome Native API session from esphome-c-api
  char mac_address[18];     // MAC address from device
  char esphome_version[16]; // ESPHome version from device
  uint32_t last_ping_ms;    // Last ping timestamp
} esphome_device_t;

typedef struct {
  char account_id[STR_SMALL];
  char pin[STR_SMALL];
  char name[STR_MEDIUM];
  char address1[STR_MEDIUM];
  char address2[STR_MEDIUM];
  char city[STR_SMALL];
  char state[STR_SMALL];
  char zip_code[STR_SMALL];
  char email[STR_MEDIUM];
  char phone[STR_SMALL];
  char instructions[STR_LARGE];
  double latitude;
  double longitude;
  int accuracy;
  char monitor_service_id[STR_MEDIUM];
  char monitor_service_key[STR_MEDIUM];
  char monitoring_url[STR_MEDIUM];
  int notify;
  char nl_ids[MAX_ALARM_SLOTS][64];
  bool is_monitor_fire;
  bool is_monitor_police;
  bool is_monitor_medical;
  bool is_monitor_other;
  char smtp_server[STR_MEDIUM];
  int smtp_port;
  char smtp_user[STR_MEDIUM];
  char smtp_pass[STR_MEDIUM];
  char mqtt_server[STR_MEDIUM];
  int mqtt_port;
  char mqtt_user[STR_SMALL];
  char mqtt_pass[STR_SMALL];
  char telegram_id[STR_SMALL];
  char telegram_token[STR_MEDIUM];
  bool is_telegram_enabled;
  char nvrserver_url[STR_MEDIUM];
  char haintegration_url[STR_MEDIUM];
  int entry_delay;
  int exit_delay;
  int cancel_delay;
} config_t;

// 3. Accessor Functions
config_t *storage_get_config(void);

int storage_get_zone_count(void);
void storage_set_zone_count(int count);
zone_t *storage_get_zone(int index);

int storage_get_relay_count(void);
void storage_set_relay_count(int count);
relay_t *storage_get_relay(int index);

int storage_get_user_count(void);
void storage_set_user_count(int count);
user_t *storage_get_user(int index);

int storage_get_esphome_count(void);
void storage_set_esphome_count(int count);
esphome_device_t *storage_get_esphome_device(int index);

// 4. Prototypes
// A helper to dump the memory map into a string instead of the console

/**
 * @brief Initialize storage system (SPIFFS mount)
 *
 * Called once during app_main(). Mounts SPIFFS filesystem where
 * configuration files are stored.
 */
void storage_init(void);

/**
 * @brief Load all configu// A helper to dump the memory map into a string
instead of the console void get_storage_debug_string(char *dest, size_t max_len)
{ int offset = 0; offset += snprintf(dest + offset, max_len - offset, "---
HARDWARE STORAGE MAP ---\n"); offset += snprintf(dest + offset, max_len -
offset, "MASTER_PIN: [%s]\n", config.pin); offset += snprintf(dest + offset,
max_len - offset, "USER_COUNT: %d\n", u_count);

    for(int i = 0; i < u_count; i++) {
        offset += snprintf(dest + offset, max_len - offset,
            "SLOT[%d]: NAME=%s PIN=%s\n", i, users[i].name, users[i].pin);

        if (offset >= max_len - 50) break; // Prevent overflow
    }
}ration from storage files
 *
 * Reads zones.json, relays.json, users.json, and config.json
 * from SPIFFS into memory structures. Validates all values
 * and applies defaults if needed.
 */
void storage_load_all(void);

/**
 * @brief Save user accounts to users.json
 *
 * Serializes current users[] array to JSON and writes to SPIFFS.
 * Called after adding/modifying users.
 */
void storage_save_users(void);

/**
 * @brief Save system configuration to config.json
 *
 * Persists all settings including network, monitoring service
 * credentials, notification preferences, and delay timings.
 */
void storage_save_config(void);

/**
 * @brief Debug print all loaded configuration
 *
 * Outputs zones, relays, users, and config to console.
 * Useful for diagnosing configuration issues.
 */
void storage_debug_print(void);

/**
 * @brief Convert zone type string to enum
 *
 * @param type_str Zone type string (e.g., "door", "motion", "fire")
 * @return zone_type_t Corresponding enum value
 */
zone_type_t zone_type_from_string(const char *type_str);

/**
 * @brief Convert zone type enum to string
 *
 * @param type Zone type enum
 * @return const char* Zone type string
 */
const char *zone_type_to_string(zone_type_t type);

/**
 * @brief Convert relay type string to enum
 *
 * @param type_str Relay type string (e.g., "alarm", "chime", "light")
 * @return relay_type_t Corresponding enum value
 */
relay_type_t relay_type_from_string(const char *type_str);

/**
 * @brief Convert relay type enum to string
 *
 * @param type Relay type enum
 * @return const char* Relay type string
 */
const char *relay_type_to_string(relay_type_t type);

/**
 * @brief Convert ESPHome device list to JSON string
 *
 * Serializes all configured ESPHome devices into JSON array format.
 * Caller must free() the returned string.
 *
 * @return char* JSON string, or NULL on error
 */
char *esphome_devices_to_json(void);

/**
 * @brief Add new ESPHome device
 *
 * Appends device to esphome_devices[] array and saves to esphome.json.
 *
 * @param hostname Device hostname or IP address
 * @param port Native API port (typically 6053)
 * @param friendly_name Display name for UI
 * @param password Native API password (can be NULL)
 * @param encryption_key Base64 encryption key (can be NULL)
 * @param virtual_zone_start Starting zone number for sensors (33-96)
 * @param virtual_relay_start Starting relay number for switches (8-31)
 * @param enabled Whether device is enabled
 * @return int 0 on success, -1 on failure
 */
int esphome_device_add(const char *hostname, uint16_t port,
                       const char *friendly_name, const char *password,
                       const char *encryption_key, int8_t virtual_zone_start,
                       int8_t virtual_relay_start, bool enabled);

/**
 * @brief Update existing ESPHome device
 *
 * Finds device by hostname and updates fields. Pass NULL/0/-1 to leave fields
 * unchanged.
 *
 * @param hostname Device hostname (search key)
 * @param port New port (0 = no change)
 * @param friendly_name New display name (NULL = no change)
 * @param password New password (NULL = no change)
 * @param encryption_key New encryption key (NULL = no change)
 * @param virtual_zone_start New zone start (-1 = no change)
 * @param virtual_relay_start New relay start (-1 = no change)
 * @param enabled Enable/disable device
 * @return int 0 on success, -1 if not found
 */
int esphome_device_update(const char *hostname, uint16_t port,
                          const char *friendly_name, const char *password,
                          const char *encryption_key, int8_t virtual_zone_start,
                          int8_t virtual_relay_start, bool enabled);

/**
 * @brief Delete ESPHome device
 *
 * Removes device from array by hostname and saves to esphome.json.
 *
 * @param hostname Device hostname to delete
 * @return int 0 on success, -1 if not found
 */
int esphome_device_delete(const char *hostname);

/**
 * @brief Save ESPHome devices to esphome.json
 *
 * Serializes esphome_devices[] array to JSON and writes to SPIFFS.
 * Called automatically by add/update/delete functions.
 *
 * @return int 0 on success, -1 on error
 */
int esphome_devices_save(void);

/**
 * @brief Authenticate user with PIN (supports both legacy and hashed)
 *
 * @param pin User's PIN to verify
 * @param out_user Output pointer to authenticated user (can be NULL)
 * @return true if authentication successful
 */
bool user_authenticate_pin(const char *pin, user_t **out_user);

/**
 * @brief Set new password for user (creates hash)
 *
 * @param user_index Index of user in users array
 * @param new_pin New PIN to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t user_set_password(int user_index, const char *new_pin);

#ifdef ESP_PLATFORM
/**
 * @brief Mount SD card via SPI
 *
 * Initializes SPI2 and mounts SD card. Used to copy updated
 * configuration files from SD to internal SPIFFS storage.
 *
 * @return esp_err_t ESP_OK if mounted successfully
 */
status_t storage_mount_sd(void);

/**
 * @brief Unmount SD card
 *
 * Called after syncing files from SD, frees resources.
 */
void storage_unmount_sd(void);

/**
 * @brief Sync configuration files from SD card to internal storage
 *
 * Copies index.html, users.html, and config.json from SD to SPIFFS
 * for configuration updates via web interface.
 */
void storage_sync_sd_to_internal(void);

#endif

#endif // STORAGE_MGR_H