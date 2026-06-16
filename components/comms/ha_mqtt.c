/**
 * @file ha_mqtt.c
 * @brief Home Assistant MQTT Discovery implementation
 */

#include "ha_mqtt.h"
#include "logging.h"
#include "storage_mgr.h"
#include "engine.h"
#include "system_monitoring.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hal.h"
#include "hal_esp32.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

static const char *TAG = "HA_MQTT";

// Forward declarations for MQTT publish
extern int mqtt_publish(const char *topic, const char *payload, int qos, bool retain);
extern void mqtt_publish_alert(const char *topic, const char *msg);

// Helper wrapper to use mqtt_publish_alert
static int _mqtt_pub(const char *topic, const char *payload) {
    if (payload) {
        mqtt_publish_alert(topic, payload);
    }
    return 0;
}

/**
 * @brief Publish Home Assistant discovery message for alarm panel
 */
static int _publish_alarm_discovery(void) {
    cJSON *config = cJSON_CreateObject();
    
    cJSON_AddStringToObject(config, "name", "Sentinel Alarm");
    cJSON_AddStringToObject(config, "unique_id", "sentinel_alarm_panel");
    cJSON_AddStringToObject(config, "state_topic", "sentinel/state");
    cJSON_AddStringToObject(config, "command_topic", "sentinel/command/arm");
    
    // Home Assistant alarm modes
    cJSON *modes = cJSON_CreateArray();
    cJSON_AddItemToArray(modes, cJSON_CreateString("disarmed"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("armed_away"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("armed_home"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("armed_night"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("triggered"));
    cJSON_AddItemToObject(config, "payload_disarm", cJSON_CreateString("disarm"));
    cJSON_AddItemToObject(config, "payload_arm_away", cJSON_CreateString("away"));
    cJSON_AddItemToObject(config, "payload_arm_home", cJSON_CreateString("stay"));
    cJSON_AddItemToObject(config, "payload_arm_night", cJSON_CreateString("night"));
    cJSON_AddItemToObject(config, "supported_code_formats", cJSON_CreateArray());
    cJSON_AddItemToArray(cJSON_GetObjectItem(config, "supported_code_formats"), cJSON_CreateString("number"));
    cJSON_AddItemToObject(config, "modes", modes);
    
    // Device info
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "Sentinel Alarm System");
    cJSON_AddStringToObject(device, "manufacturer", "Sentinel-ESP");
    cJSON_AddStringToObject(device, "model", "KC868-A8V3");
    cJSON_AddItemToObject(config, "device", device);
    
    char *payload = cJSON_Print(config);
    int ret = _mqtt_pub("homeassistant/alarm_control_panel/sentinel/config", payload);
    free(payload);
    cJSON_Delete(config);
    
    return ret;
}

/**
 * @brief Publish Home Assistant discovery message for temperature sensor
 */
static int _publish_temp_discovery(void) {
    cJSON *config = cJSON_CreateObject();
    
    cJSON_AddStringToObject(config, "name", "Sentinel Temperature");
    cJSON_AddStringToObject(config, "unique_id", "sentinel_temperature");
    cJSON_AddStringToObject(config, "state_topic", "sentinel/temperature");
    cJSON_AddStringToObject(config, "unit_of_measurement", "°F");
    cJSON_AddStringToObject(config, "device_class", "temperature");
    
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "Sentinel Alarm System");
    cJSON_AddItemToObject(config, "device", device);
    
    char *payload = cJSON_Print(config);
    int ret = _mqtt_pub("homeassistant/sensor/sentinel_temp/config", payload);
    free(payload);
    cJSON_Delete(config);
    
    return ret;
}

/**
 * @brief Publish Home Assistant discovery message for tamper sensor
 */
static int _publish_tamper_discovery(void) {
    cJSON *config = cJSON_CreateObject();
    
    cJSON_AddStringToObject(config, "name", "Sentinel Tamper");
    cJSON_AddStringToObject(config, "unique_id", "sentinel_tamper");
    cJSON_AddStringToObject(config, "state_topic", "sentinel/tamper");
    cJSON_AddStringToObject(config, "device_class", "tamper");
    cJSON_AddStringToObject(config, "payload_on", "detected");
    cJSON_AddStringToObject(config, "payload_off", "safe");
    
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "Sentinel Alarm System");
    cJSON_AddItemToObject(config, "device", device);
    
    char *payload = cJSON_Print(config);
    int ret = _mqtt_pub("homeassistant/binary_sensor/sentinel_tamper/config", payload);
    free(payload);
    cJSON_Delete(config);
    
    return ret;
}

/**
 * @brief Publish Home Assistant discovery message for power sensor
 */
static int _publish_power_discovery(void) {
    cJSON *config = cJSON_CreateObject();
    
    cJSON_AddStringToObject(config, "name", "Sentinel Power Status");
    cJSON_AddStringToObject(config, "unique_id", "sentinel_power");
    cJSON_AddStringToObject(config, "state_topic", "sentinel/power");
    cJSON_AddStringToObject(config, "device_class", "power");
    cJSON_AddStringToObject(config, "payload_on", "backup");
    cJSON_AddStringToObject(config, "payload_off", "normal");
    
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "Sentinel Alarm System");
    cJSON_AddItemToObject(config, "device", device);
    
    char *payload = cJSON_Print(config);
    int ret = _mqtt_pub("homeassistant/binary_sensor/sentinel_power/config", payload);
    free(payload);
    cJSON_Delete(config);
    
    return ret;
}

/**
 * @brief Publish Home Assistant discovery messages for relays
 */
static int _publish_relays_discovery(void) {
    for (int i = 0; i < storage_get_relay_count() && i < 8; i++) {
        cJSON *config = cJSON_CreateObject();
        
        char name[64];
        snprintf(name, sizeof(name), "Sentinel %.50s", storage_get_relay(i)->name);
        cJSON_AddStringToObject(config, "name", name);
        
        char unique_id[64];
        snprintf(unique_id, sizeof(unique_id), "sentinel_relay_%d", i);
        cJSON_AddStringToObject(config, "unique_id", unique_id);
        
        char state_topic[64];
        snprintf(state_topic, sizeof(state_topic), "sentinel/relay/%d/state", i);
        cJSON_AddStringToObject(config, "state_topic", state_topic);
        
        char cmd_topic[64];
        snprintf(cmd_topic, sizeof(cmd_topic), "sentinel/relay/%d/command", i);
        cJSON_AddStringToObject(config, "command_topic", cmd_topic);
        
        cJSON_AddStringToObject(config, "payload_on", "ON");
        cJSON_AddStringToObject(config, "payload_off", "OFF");
        
        cJSON *device = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "name", "Sentinel Alarm System");
        cJSON_AddItemToObject(config, "device", device);
        
        char *payload = cJSON_Print(config);
        char discovery_topic[128];
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/switch/sentinel_relay_%d/config", i);
        _mqtt_pub(discovery_topic, payload);
        free(payload);
        cJSON_Delete(config);
    }
    
    return 0;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int ha_mqtt_init(void) {
    LOG_INFO(TAG, "Publishing Home Assistant discovery messages");
    
    _publish_alarm_discovery();
    _publish_temp_discovery();
    _publish_tamper_discovery();
    _publish_power_discovery();
    _publish_relays_discovery();
    
    return 0;
}

int ha_mqtt_publish_state(void) {
    int arm_state = engine_get_arm_state();
    const char *state_str = "unknown";
    
    switch (arm_state) {
        case 0: state_str = "disarmed"; break;
        case 1: state_str = "armed_away"; break;  // Exit delay
        case 2: state_str = "armed_away"; break;  // Armed
        case 3: state_str = "armed_away"; break;  // Entry delay
        case 4: state_str = "triggered"; break;   // Alarmed
    }
    
    _mqtt_pub("sentinel/state", state_str);
    
    // Also publish individual states
    system_status_t status = {0};
    system_monitoring_get_status(&status);
    
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%d", (int)((status.temperature_c * 9 / 5) + 32));
    _mqtt_pub("sentinel/temperature", temp_str);
    
    _mqtt_pub("sentinel/tamper", status.tamper_detected ? "detected" : "safe");
    _mqtt_pub("sentinel/power", status.on_backup_power ? "backup" : "normal");
    
    // Publish relay states
    for (int i = 0; i < storage_get_relay_count() && i < 8; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "sentinel/relay/%d/state", i);
        int relay_state = hal_get_relay_state(storage_get_relay(i)->gpio);
        _mqtt_pub(topic, relay_state ? "ON" : "OFF");
    }
    
    // Publish zone states
    for (int i = 0; i < storage_get_zone_count() && i < 16; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "sentinel/zone/%d/state", i);
        int zone_state = hal_get_zone_state(storage_get_zone(i)->gpio);
        _mqtt_pub(topic, zone_state ? "SECURE" : "OPEN");
    }
    
    return 0;
}

int ha_mqtt_publish_arm_state(const char *state_str) {
    if (!state_str) return -1;
    return _mqtt_pub("sentinel/state", state_str) == 0 ? 0 : -1;
}

int ha_mqtt_publish_relay_state(uint8_t relay_index, bool state) {
    if (relay_index >= storage_get_relay_count() || relay_index >= 8) return -1;
    
    char topic[64];
    snprintf(topic, sizeof(topic), "sentinel/relay/%d/state", relay_index);
    return _mqtt_pub(topic, state ? "ON" : "OFF") == 0 ? 0 : -1;
}

int ha_mqtt_publish_temperature(int temp_f) {
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%d", temp_f);
    return _mqtt_pub("sentinel/temperature", temp_str) == 0 ? 0 : -1;
}

int ha_mqtt_publish_tamper(bool detected) {
    return _mqtt_pub("sentinel/tamper", detected ? "detected" : "safe") == 0 ? 0 : -1;
}

int ha_mqtt_publish_power(bool on_backup) {
    return _mqtt_pub("sentinel/power", on_backup ? "backup" : "normal") == 0 ? 0 : -1;
}

int ha_mqtt_publish_zone_state(uint8_t zone_index, bool open) {
    if (zone_index >= storage_get_zone_count() || zone_index >= 16) return -1;
    
    char topic[64];
    snprintf(topic, sizeof(topic), "sentinel/zone/%d/state", zone_index);
    return _mqtt_pub(topic, open ? "OPEN" : "SECURE") == 0 ? 0 : -1;
}

int ha_mqtt_handle_command(const char *topic, const uint8_t *payload, uint32_t payload_len) {
    if (!topic || !payload) return -1;
    
    char payload_str[256] = {0};
    if (payload_len > sizeof(payload_str) - 1) payload_len = sizeof(payload_str) - 1;
    memcpy(payload_str, payload, payload_len);
    
    LOG_DEBUG(TAG, "HA command: %s = %s", topic, payload_str);
    
    // Handle arm commands
    if (strcmp(topic, "sentinel/command/arm") == 0) {
        if (strcmp(payload_str, "away") == 0) {
            engine_ui_arm(ARM_AWAY);
        } else if (strcmp(payload_str, "stay") == 0) {
            engine_ui_arm(ARM_STAY);
        } else if (strcmp(payload_str, "night") == 0) {
            engine_ui_arm(ARM_NIGHT);
        } else if (strcmp(payload_str, "vacation") == 0) {
            engine_ui_arm(ARM_VACATION);
        }
        return 0;
    }
    
    // Handle disarm command
    if (strcmp(topic, "sentinel/command/disarm") == 0) {
        engine_ui_disarm();
        return 0;
    }
    
    // Handle relay commands
    if (strstr(topic, "sentinel/relay/") && strstr(topic, "/command")) {
        int relay_idx = -1;
        sscanf(topic, "sentinel/relay/%d/command", &relay_idx);
        
        if (relay_idx >= 0 && relay_idx < storage_get_relay_count()) {
            bool on = strcmp(payload_str, "ON") == 0;
            hal_set_relay(storage_get_relay(relay_idx)->gpio, on);
            ha_mqtt_publish_relay_state(relay_idx, on);
            return 0;
        }
    }
    
    return -1;
}
