/**
 * @file ha_mqtt.h
 * @brief Home Assistant MQTT Discovery integration
 * 
 * Publishes Sentinel alarm system state and configuration to Home Assistant
 * using MQTT Discovery protocol. Allows Home Assistant to automatically
 * discover and control the alarm system.
 * 
 * Topics Published:
 * - homeassistant/alarm_control_panel/sentinel/config - Alarm config
 * - homeassistant/sensor/sentinel_temp/config - Temperature sensor
 * - homeassistant/binary_sensor/sentinel_tamper/config - Tamper sensor
 * - homeassistant/binary_sensor/sentinel_power/config - Power loss sensor
 * - homeassistant/switch/sentinel_relay_N/config - Relay switches (N=0-7)
 * - sentinel/state - Current alarm state
 * - sentinel/temperature - Temperature reading
 * - sentinel/tamper - Tamper status
 * - sentinel/power - Power status
 * - sentinel/relay/N/state - Relay states (N=0-7)
 * 
 * @author Sentinel-ESP Team
 * @version 1.0.0
 * @date 2026-01-21
 */

#ifndef HA_MQTT_H
#define HA_MQTT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Home Assistant MQTT Discovery
 * 
 * Publishes discovery messages for all Sentinel entities (alarm, sensors, relays).
 * Called once during startup. Home Assistant subscribes to homeassistant/# topics
 * and auto-discovers devices.
 * 
 * @return 0 on success, error code otherwise
 * 
 * @example
 * ```c
 * if (ha_mqtt_init() == 0) {
 *     ESP_LOGI(TAG, "Home Assistant discovery published");
 * }
 * ```
 */
int ha_mqtt_init(void);

/**
 * @brief Publish current system state to Home Assistant
 * 
 * Updates all entity states on MQTT:
 * - Alarm arm/disarm state
 * - Temperature reading
 * - Tamper/power status
 * - Relay states
 * - Zone states
 * 
 * Should be called periodically or when state changes.
 * 
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_state(void);

/**
 * @brief Publish alarm state change
 * 
 * Updates Home Assistant alarm panel with new state:
 * - "disarmed" - System off
 * - "armed_away" - Away mode
 * - "armed_home" - Stay mode
 * - "armed_night" - Night mode
 * - "triggered" - Alarm active
 * 
 * @param state_str State string (see above)
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_arm_state(const char *state_str);

/**
 * @brief Publish relay state to Home Assistant
 * 
 * Updates individual relay switch state in Home Assistant.
 * 
 * @param relay_index Relay index (0-7 for KC868-A8V3)
 * @param state true = on, false = off
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_relay_state(uint8_t relay_index, bool state);

/**
 * @brief Publish temperature reading to Home Assistant
 * 
 * Updates temperature sensor in Home Assistant.
 * 
 * @param temp_f Temperature in Fahrenheit
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_temperature(int temp_f);

/**
 * @brief Publish tamper detection status
 * 
 * Updates tamper binary sensor in Home Assistant.
 * 
 * @param detected true = tamper detected, false = no tamper
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_tamper(bool detected);

/**
 * @brief Publish power loss status
 * 
 * Updates power loss binary sensor in Home Assistant.
 * 
 * @param on_backup true = on backup power, false = main power
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_power(bool on_backup);

/**
 * @brief Publish zone state to Home Assistant
 * 
 * Updates individual zone sensor in Home Assistant.
 * 
 * @param zone_index Zone index
 * @param open true = zone open/violated, false = secure
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_publish_zone_state(uint8_t zone_index, bool open);

/**
 * @brief Handle Home Assistant commands from MQTT
 * 
 * Processes commands received from Home Assistant on:
 * - sentinel/command/arm - Arm system (payload: "away", "stay", "night")
 * - sentinel/command/disarm - Disarm system (payload: PIN)
 * - sentinel/command/relay/N - Control relays (payload: "on"/"off")
 * 
 * Called when messages arrive on command topics.
 * 
 * @param topic MQTT topic
 * @param payload Command payload
 * @param payload_len Payload length
 * @return 0 on success, error code otherwise
 */
int ha_mqtt_handle_command(const char *topic, const uint8_t *payload, uint32_t payload_len);

#ifdef __cplusplus
}
#endif

#endif // HA_MQTT_H
