/**
 * @file tuya_protocol.c
 * @brief Tuya UART Protocol Implementation
 */

#include "tuya_protocol.h"
#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "tuya_proto";

// UART handle
static bool uart_initialized = false;

esp_err_t tuya_protocol_init(void) {
    if (uart_initialized) {
        ESP_LOGW(TAG, "UART already initialized");
        return ESP_OK;
    }

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = TUYA_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(TUYA_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %d", ret);
        return ret;
    }

    // Set UART pins
    ret = uart_set_pin(TUYA_UART_NUM, 
                       TUYA_UART_TX_GPIO, 
                       TUYA_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, 
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return ret;
    }

    // Install UART driver
    ret = uart_driver_install(TUYA_UART_NUM, 
                              TUYA_UART_BUF_SIZE, 
                              TUYA_UART_BUF_SIZE, 
                              0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return ret;
    }

    uart_initialized = true;
    ESP_LOGI(TAG, "Tuya UART initialized (TX=GPIO%d, RX=GPIO%d, baud=%d)",
             TUYA_UART_TX_GPIO, TUYA_UART_RX_GPIO, TUYA_UART_BAUD_RATE);

    return ESP_OK;
}

void tuya_protocol_deinit(void) {
    if (uart_initialized) {
        uart_driver_delete(TUYA_UART_NUM);
        uart_initialized = false;
        ESP_LOGI(TAG, "Tuya UART deinitialized");
    }
}

uint8_t tuya_protocol_checksum(const uint8_t *data, uint16_t len) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

esp_err_t tuya_protocol_encode_frame(tuya_command_t command,
                                      const uint8_t *data,
                                      uint16_t data_len,
                                      uint8_t *out_buf,
                                      uint16_t *out_len) {
    if (!out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data_len > TUYA_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Data length %d exceeds maximum %d", data_len, TUYA_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t frame_len = TUYA_FRAME_MIN_LEN + data_len;
    
    // Build frame
    out_buf[0] = TUYA_FRAME_HEADER_H;
    out_buf[1] = TUYA_FRAME_HEADER_L;
    out_buf[2] = TUYA_PROTOCOL_VERSION;
    out_buf[3] = (uint8_t)command;
    out_buf[4] = (data_len >> 8) & 0xFF;  // Length high byte
    out_buf[5] = data_len & 0xFF;         // Length low byte
    
    // Copy data payload
    if (data && data_len > 0) {
        memcpy(&out_buf[6], data, data_len);
    }
    
    // Calculate and append checksum
    uint8_t checksum = tuya_protocol_checksum(out_buf, frame_len - 1);
    out_buf[frame_len - 1] = checksum;
    
    *out_len = frame_len;
    
    ESP_LOGD(TAG, "Encoded frame: cmd=0x%02X, len=%d, checksum=0x%02X", 
             command, data_len, checksum);
    
    return ESP_OK;
}

esp_err_t tuya_protocol_decode_frame(const uint8_t *in_buf,
                                       uint16_t in_len,
                                       tuya_parsed_frame_t *frame) {
    if (!in_buf || !frame || in_len < TUYA_FRAME_MIN_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verify header
    if (in_buf[0] != TUYA_FRAME_HEADER_H || in_buf[1] != TUYA_FRAME_HEADER_L) {
        ESP_LOGW(TAG, "Invalid frame header: 0x%02X 0x%02X", in_buf[0], in_buf[1]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Verify version
    if (in_buf[2] != TUYA_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Unsupported protocol version: 0x%02X", in_buf[2]);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Extract command and length
    frame->command = (tuya_command_t)in_buf[3];
    frame->data_len = (in_buf[4] << 8) | in_buf[5];

    // Verify frame length
    uint16_t expected_len = TUYA_FRAME_MIN_LEN + frame->data_len;
    if (in_len < expected_len) {
        ESP_LOGW(TAG, "Frame length mismatch: got %d, expected %d", in_len, expected_len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Verify checksum
    uint8_t calc_checksum = tuya_protocol_checksum(in_buf, expected_len - 1);
    uint8_t recv_checksum = in_buf[expected_len - 1];
    if (calc_checksum != recv_checksum) {
        ESP_LOGW(TAG, "Checksum error: calc=0x%02X, recv=0x%02X", 
                 calc_checksum, recv_checksum);
        return ESP_ERR_INVALID_CRC;
    }

    // Copy data payload
    if (frame->data_len > 0) {
        if (frame->data_len > TUYA_MAX_DATA_LEN) {
            ESP_LOGW(TAG, "Data length %d exceeds buffer size", frame->data_len);
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(frame->data, &in_buf[6], frame->data_len);
    }

    ESP_LOGD(TAG, "Decoded frame: cmd=0x%02X, len=%d", frame->command, frame->data_len);

    return ESP_OK;
}

esp_err_t tuya_protocol_send_command(tuya_command_t command,
                                      const uint8_t *data,
                                      uint16_t data_len) {
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t frame_buf[TUYA_FRAME_MIN_LEN + TUYA_MAX_DATA_LEN];
    uint16_t frame_len;

    esp_err_t ret = tuya_protocol_encode_frame(command, data, data_len, 
                                                frame_buf, &frame_len);
    if (ret != ESP_OK) {
        return ret;
    }

    int written = uart_write_bytes(TUYA_UART_NUM, frame_buf, frame_len);
    if (written != frame_len) {
        ESP_LOGE(TAG, "UART write failed: wrote %d of %d bytes", written, frame_len);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent command 0x%02X (%d bytes)", command, frame_len);

    return ESP_OK;
}

esp_err_t tuya_protocol_receive_frame(tuya_parsed_frame_t *frame,
                                       uint32_t timeout_ms) {
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t recv_buf[TUYA_FRAME_MIN_LEN + TUYA_MAX_DATA_LEN];
    int recv_len = 0;

    // Wait for frame header
    int timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    recv_len = uart_read_bytes(TUYA_UART_NUM, recv_buf, 
                               TUYA_FRAME_MIN_LEN + TUYA_MAX_DATA_LEN,
                               timeout_ticks);

    if (recv_len < TUYA_FRAME_MIN_LEN) {
        if (recv_len > 0) {
            ESP_LOGD(TAG, "Received incomplete frame: %d bytes", recv_len);
        }
        return ESP_ERR_TIMEOUT;
    }

    // Decode frame
    esp_err_t ret = tuya_protocol_decode_frame(recv_buf, recv_len, frame);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to decode frame: %d", ret);
        return ret;
    }

    return ESP_OK;
}

esp_err_t tuya_protocol_send_heartbeat(void) {
    ESP_LOGD(TAG, "Sending heartbeat");
    return tuya_protocol_send_command(TUYA_CMD_HEARTBEAT, NULL, 0);
}

esp_err_t tuya_protocol_query_product(void) {
    ESP_LOGD(TAG, "Querying product info");
    return tuya_protocol_send_command(TUYA_CMD_QUERY_PRODUCT, NULL, 0);
}

esp_err_t tuya_protocol_report_status(const tuya_dp_t *dp_array, uint8_t dp_count) {
    if (!dp_array || dp_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload[TUYA_MAX_DATA_LEN];
    uint16_t payload_len = 0;

    for (uint8_t i = 0; i < dp_count; i++) {
        const tuya_dp_t *dp = &dp_array[i];
        
        if (payload_len + 4 + dp->dp_len > TUYA_MAX_DATA_LEN) {
            ESP_LOGW(TAG, "Payload buffer full, truncating status report");
            break;
        }

        // Encode DP: ID + Type + Length(2) + Data
        payload[payload_len++] = dp->dp_id;
        payload[payload_len++] = dp->dp_type;
        payload[payload_len++] = (dp->dp_len >> 8) & 0xFF;
        payload[payload_len++] = dp->dp_len & 0xFF;
        memcpy(&payload[payload_len], dp->dp_data, dp->dp_len);
        payload_len += dp->dp_len;
    }

    ESP_LOGD(TAG, "Reporting status: %d DPs, payload %d bytes", dp_count, payload_len);
    return tuya_protocol_send_command(TUYA_CMD_STATUS_REPORT, payload, payload_len);
}

esp_err_t tuya_protocol_reset_wifi(void) {
    ESP_LOGI(TAG, "Resetting WiFi configuration");
    return tuya_protocol_send_command(TUYA_CMD_WIFI_RESET, NULL, 0);
}

esp_err_t tuya_protocol_parse_dp(const uint8_t *data, uint16_t data_len, tuya_dp_t *dp) {
    if (!data || !dp || data_len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    dp->dp_id = data[0];
    dp->dp_type = data[1];
    dp->dp_len = (data[2] << 8) | data[3];

    if (data_len < 4 + dp->dp_len) {
        ESP_LOGW(TAG, "DP data length mismatch");
        return ESP_ERR_INVALID_SIZE;
    }

    if (dp->dp_len > sizeof(dp->dp_data)) {
        ESP_LOGW(TAG, "DP data too large: %d bytes", dp->dp_len);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(dp->dp_data, &data[4], dp->dp_len);

    ESP_LOGD(TAG, "Parsed DP: id=%d, type=%d, len=%d", dp->dp_id, dp->dp_type, dp->dp_len);

    return ESP_OK;
}

esp_err_t tuya_protocol_encode_dp(const tuya_dp_t *dp, uint8_t *out_buf, uint16_t *out_len) {
    if (!dp || !out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t total_len = 4 + dp->dp_len;
    
    out_buf[0] = dp->dp_id;
    out_buf[1] = dp->dp_type;
    out_buf[2] = (dp->dp_len >> 8) & 0xFF;
    out_buf[3] = dp->dp_len & 0xFF;
    memcpy(&out_buf[4], dp->dp_data, dp->dp_len);

    *out_len = total_len;

    return ESP_OK;
}
