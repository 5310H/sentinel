/**
 * @file tuya_protocol.h
 * @brief Tuya UART Protocol Implementation
 * 
 * Implements the Tuya MCU SDK UART protocol for communication
 * with the Tuya CBU WiFi module on KC868-A8.
 * 
 * Hardware Connections:
 * - Tuya RX: GPIO17 (ESP32 TX)
 * - Tuya TX: GPIO16 (ESP32 RX)
 * - Network Button: P28
 * - Network LED: P16
 */

#ifndef TUYA_PROTOCOL_H
#define TUYA_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Protocol Constants
#define TUYA_FRAME_HEADER_H         0x55
#define TUYA_FRAME_HEADER_L         0xAA
#define TUYA_PROTOCOL_VERSION       0x03
#define TUYA_MAX_DATA_LEN           256
#define TUYA_FRAME_MIN_LEN          7   // Header(2) + Ver(1) + Cmd(1) + Len(2) + Checksum(1)

// GPIO Configuration
#define TUYA_UART_TX_GPIO           17
#define TUYA_UART_RX_GPIO           16
#define TUYA_UART_NUM               UART_NUM_1
#define TUYA_UART_BAUD_RATE         9600
#define TUYA_UART_BUF_SIZE          1024

// Tuya Commands
typedef enum {
    TUYA_CMD_HEARTBEAT          = 0x00,  // Heartbeat request/response
    TUYA_CMD_QUERY_PRODUCT      = 0x01,  // Query product information
    TUYA_CMD_QUERY_MODE         = 0x02,  // Query working mode
    TUYA_CMD_WIFI_STATE         = 0x03,  // Report WiFi status
    TUYA_CMD_WIFI_RESET         = 0x04,  // Reset WiFi
    TUYA_CMD_WIFI_SELECT        = 0x05,  // Select WiFi mode
    TUYA_CMD_STATUS_REPORT      = 0x06,  // Report device status
    TUYA_CMD_STATUS_QUERY       = 0x07,  // Query device status (from cloud)
    TUYA_CMD_STATUS_QUERY_RESP  = 0x08,  // Status query response
    TUYA_CMD_LOCAL_TIME_QUERY   = 0x1C,  // Query local time
    TUYA_CMD_LOCAL_TIME         = 0x1C,  // Local time sync
    TUYA_CMD_GET_ONLINE_TIME    = 0x24,  // Get online time duration
    TUYA_CMD_FACTORY_TEST       = 0x25,  // Factory test mode
    TUYA_CMD_WEATHER_DATA       = 0x34,  // Weather data
    TUYA_CMD_GET_MEMORY         = 0x3F,  // Get memory info
} tuya_command_t;

// WiFi Status
typedef enum {
    TUYA_WIFI_STATUS_1          = 0x00,  // SmartConfig pairing mode
    TUYA_WIFI_STATUS_2          = 0x01,  // AP pairing mode
    TUYA_WIFI_STATUS_3          = 0x02,  // WiFi configured, not connected
    TUYA_WIFI_STATUS_4          = 0x03,  // WiFi connected, not registered
    TUYA_WIFI_STATUS_5          = 0x04,  // WiFi connected and registered
    TUYA_WIFI_STATUS_6          = 0x05,  // Low power mode
    TUYA_WIFI_STATUS_7          = 0x06,  // SmartConfig and AP both active
} tuya_wifi_status_t;

// Data Point Types
typedef enum {
    TUYA_DP_TYPE_RAW            = 0x00,  // Raw data
    TUYA_DP_TYPE_BOOL           = 0x01,  // Boolean (1 byte)
    TUYA_DP_TYPE_VALUE          = 0x02,  // Integer (4 bytes)
    TUYA_DP_TYPE_STRING         = 0x03,  // String
    TUYA_DP_TYPE_ENUM           = 0x04,  // Enum (1 byte)
    TUYA_DP_TYPE_BITMAP         = 0x05,  // Bitmap (1/2/4 bytes)
} tuya_dp_type_t;

// Tuya Frame Structure
typedef struct __attribute__((packed)) {
    uint8_t header[2];          // 0x55 0xAA
    uint8_t version;            // Protocol version (0x03)
    uint8_t command;            // Command type
    uint16_t length;            // Data length (big endian)
    uint8_t data[];             // Variable length data + checksum at end
} tuya_frame_t;

// Data Point Structure
typedef struct {
    uint8_t dp_id;              // Data point ID
    uint8_t dp_type;            // Data point type
    uint16_t dp_len;            // Data length (big endian)
    uint8_t dp_data[32];        // Data value (max 32 bytes for common types)
} tuya_dp_t;

// Parsed Frame Structure
typedef struct {
    tuya_command_t command;
    uint16_t data_len;
    uint8_t data[TUYA_MAX_DATA_LEN];
} tuya_parsed_frame_t;

/**
 * @brief Initialize Tuya UART protocol
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t tuya_protocol_init(void);

/**
 * @brief Deinitialize Tuya UART protocol
 */
void tuya_protocol_deinit(void);

/**
 * @brief Calculate checksum for Tuya frame
 * @param data Pointer to frame data (excluding checksum)
 * @param len Length of data
 * @return Calculated checksum
 */
uint8_t tuya_protocol_checksum(const uint8_t *data, uint16_t len);

/**
 * @brief Encode a Tuya frame
 * @param command Command type
 * @param data Data payload
 * @param data_len Length of data payload
 * @param out_buf Output buffer for encoded frame
 * @param out_len Pointer to store output frame length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t tuya_protocol_encode_frame(tuya_command_t command, 
                                      const uint8_t *data, 
                                      uint16_t data_len,
                                      uint8_t *out_buf, 
                                      uint16_t *out_len);

/**
 * @brief Decode a Tuya frame
 * @param in_buf Input buffer with raw UART data
 * @param in_len Length of input buffer
 * @param frame Output parsed frame
 * @return ESP_OK on success, ESP_ERR_INVALID_CRC on checksum error, 
 *         ESP_ERR_INVALID_SIZE on malformed frame
 */
esp_err_t tuya_protocol_decode_frame(const uint8_t *in_buf, 
                                       uint16_t in_len,
                                       tuya_parsed_frame_t *frame);

/**
 * @brief Send a command to Tuya module
 * @param command Command type
 * @param data Optional data payload
 * @param data_len Length of data payload
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t tuya_protocol_send_command(tuya_command_t command, 
                                      const uint8_t *data, 
                                      uint16_t data_len);

/**
 * @brief Receive and parse a frame from Tuya module
 * @param frame Output parsed frame
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t tuya_protocol_receive_frame(tuya_parsed_frame_t *frame, 
                                       uint32_t timeout_ms);

/**
 * @brief Send heartbeat to Tuya module
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_send_heartbeat(void);

/**
 * @brief Query product information
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_query_product(void);

/**
 * @brief Report device status to Tuya cloud
 * @param dp_array Array of data points
 * @param dp_count Number of data points
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_report_status(const tuya_dp_t *dp_array, uint8_t dp_count);

/**
 * @brief Reset WiFi configuration
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_reset_wifi(void);

/**
 * @brief Parse data point from raw data
 * @param data Raw data buffer
 * @param data_len Length of raw data
 * @param dp Output data point structure
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_parse_dp(const uint8_t *data, uint16_t data_len, tuya_dp_t *dp);

/**
 * @brief Encode data point to raw data
 * @param dp Data point structure
 * @param out_buf Output buffer
 * @param out_len Pointer to store output length
 * @return ESP_OK on success
 */
esp_err_t tuya_protocol_encode_dp(const tuya_dp_t *dp, uint8_t *out_buf, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // TUYA_PROTOCOL_H
