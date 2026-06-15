/**
 * @file zigbee_protocol.h
 * @brief Low-level Zigbee coordinator communication protocol
 * 
 * Handles UART communication with external Zigbee coordinator modules
 * connected to KC868-A8 GPIO pins (no USB required).
 * 
 * Supported coordinators:
 * - TI CC2652P/CC2652RB (UART, 115200 baud)
 * - Silicon Labs EFR32 (UART, 115200 baud)
 * - Generic Zigbee2MQTT-compatible coordinators
 * 
 * Hardware connection:
 * - Coordinator TX → ESP32 GPIO19 (RX)
 * - Coordinator RX → ESP32 GPIO18 (TX)
 * - Power: 3.3V from KC868-A8
 * - GND: Common ground
 */

#ifndef ZIGBEE_PROTOCOL_H
#define ZIGBEE_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Zigbee coordinator types (all UART-based, no USB)
typedef enum {
    ZIGBEE_COORDINATOR_NONE = 0,
    ZIGBEE_COORDINATOR_CC2652P,     // TI CC2652P (Z-Stack, UART 115200)
    ZIGBEE_COORDINATOR_CC2652RB,    // TI CC2652RB (Z-Stack, UART 115200)
    ZIGBEE_COORDINATOR_EFR32,       // Silicon Labs EFR32MG (EmberZNet, UART 115200)
    ZIGBEE_COORDINATOR_ZB_MODULE,   // Generic Zigbee module (UART)
    ZIGBEE_COORDINATOR_GENERIC      // Generic coordinator (auto-detect)
} zigbee_coordinator_type_t;

// Zigbee frame types
typedef enum {
    ZIGBEE_FRAME_DATA = 0x00,
    ZIGBEE_FRAME_ACK = 0x01,
    ZIGBEE_FRAME_NACK = 0x02,
    ZIGBEE_FRAME_DEVICE_ANNOUNCE = 0x10,
    ZIGBEE_FRAME_ATTRIBUTE_REPORT = 0x11,
    ZIGBEE_FRAME_COMMAND = 0x12,
    ZIGBEE_FRAME_READ_ATTRIBUTE = 0x20,
    ZIGBEE_FRAME_WRITE_ATTRIBUTE = 0x21,
    ZIGBEE_FRAME_DISCOVERY = 0x30,
    ZIGBEE_FRAME_PERMIT_JOIN = 0x31
} zigbee_frame_type_t;

// Zigbee cluster IDs (common clusters)
#define ZIGBEE_CLUSTER_ON_OFF           0x0006
#define ZIGBEE_CLUSTER_LEVEL_CONTROL    0x0008
#define ZIGBEE_CLUSTER_COLOR_CONTROL    0x0300
#define ZIGBEE_CLUSTER_TEMP_MEASUREMENT 0x0402
#define ZIGBEE_CLUSTER_IAS_ZONE         0x0500
#define ZIGBEE_CLUSTER_DOOR_LOCK        0x0101

// Zigbee frame structure
typedef struct {
    uint8_t frame_control;
    uint8_t sequence;
    uint16_t short_addr;        // Device short address
    uint8_t endpoint;
    uint16_t cluster_id;
    uint8_t command_id;
    uint8_t *payload;
    size_t payload_len;
} zigbee_frame_t;

// Configuration
typedef struct {
    int uart_num;               // UART port number (UART_NUM_1, UART_NUM_2)
    int tx_pin;                 // GPIO for TX
    int rx_pin;                 // GPIO for RX
    int baud_rate;              // Baud rate (115200 typical)
    zigbee_coordinator_type_t coordinator_type;
} zigbee_config_t;

// Callback for received frames
typedef void (*zigbee_frame_callback_t)(const zigbee_frame_t *frame);

/**
 * @brief Initialize Zigbee protocol layer
 * @param config Configuration (NULL for defaults: UART1, GPIO18/19, 115200 baud)
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_init(const zigbee_config_t *config);

/**
 * @brief Deinitialize Zigbee protocol
 */
void zigbee_protocol_deinit(void);

/**
 * @brief Check if coordinator is connected and responsive
 * @return true if connected
 */
bool zigbee_protocol_is_connected(void);

/**
 * @brief Get coordinator type
 * @return Coordinator type (auto-detected on init)
 */
zigbee_coordinator_type_t zigbee_protocol_get_coordinator_type(void);

/**
 * @brief Send raw frame to coordinator
 * @param frame Frame to send
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_frame(const zigbee_frame_t *frame);

/**
 * @brief Set callback for received frames
 * @param callback Function to call when frame received
 */
void zigbee_protocol_set_callback(zigbee_frame_callback_t callback);

/**
 * @brief Enable permit join mode (allow new devices to pair)
 * @param duration Duration in seconds (0 = disable, 255 = forever)
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_permit_join(uint8_t duration);

/**
 * @brief Request device discovery/refresh
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_discover_devices(void);

/**
 * @brief Send ON/OFF command to device
 * @param short_addr Device short address
 * @param endpoint Endpoint number (typically 1)
 * @param on true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_on_off(uint16_t short_addr, uint8_t endpoint, bool on);

/**
 * @brief Send level control command (brightness)
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param level Level 0-254
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_level(uint16_t short_addr, uint8_t endpoint, uint8_t level);

/**
 * @brief Send color control command (RGB)
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param hue Hue 0-254
 * @param saturation Saturation 0-254
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_color(uint16_t short_addr, uint8_t endpoint, 
                                     uint8_t hue, uint8_t saturation);

/**
 * @brief Send color temperature command
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param mireds Color temperature in mireds
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_color_temp(uint16_t short_addr, uint8_t endpoint, 
                                          uint16_t mireds);

/**
 * @brief Send lock/unlock command
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param locked true = lock, false = unlock
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_lock(uint16_t short_addr, uint8_t endpoint, bool locked);

/**
 * @brief Send thermostat setpoint command
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param temperature Temperature in 0.01°C units (e.g., 2100 = 21.00°C)
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_send_thermostat_setpoint(uint16_t short_addr, uint8_t endpoint, 
                                                    int16_t temperature);

/**
 * @brief Read attribute from device
 * @param short_addr Device short address
 * @param endpoint Endpoint number
 * @param cluster_id Cluster ID
 * @param attr_id Attribute ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_protocol_read_attribute(uint16_t short_addr, uint8_t endpoint, 
                                         uint16_t cluster_id, uint16_t attr_id);

#ifdef __cplusplus
}
#endif

#endif // ZIGBEE_PROTOCOL_H
