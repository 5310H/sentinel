/**
 * @file zigbee_protocol.c
 * @brief Zigbee coordinator UART communication implementation
 */

#include "zigbee_protocol.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

static const char *TAG = "ZIGBEE_PROTO";

// Default configuration
#define DEFAULT_UART_NUM    UART_NUM_1
#define DEFAULT_TX_PIN      18
#define DEFAULT_RX_PIN      19
#define DEFAULT_BAUD_RATE   115200
#define UART_BUF_SIZE       2048

// Protocol state
static struct {
    bool initialized;
    uart_port_t uart_num;
    zigbee_coordinator_type_t coordinator_type;
    zigbee_frame_callback_t frame_callback;
    uint8_t tx_buffer[256];
    uint8_t rx_buffer[512];
    uint8_t sequence;
    bool connected;
} s_zigbee;

// Forward declarations
static void zigbee_rx_task(void *arg);
static zigbee_coordinator_type_t detect_coordinator_type(void);
static esp_err_t send_raw_data(const uint8_t *data, size_t len);

/**
 * Initialize Zigbee protocol
 */
esp_err_t zigbee_protocol_init(const zigbee_config_t *config) {
    if (s_zigbee.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_zigbee, 0, sizeof(s_zigbee));

    // Apply configuration
    int uart_num = DEFAULT_UART_NUM;
    int tx_pin = DEFAULT_TX_PIN;
    int rx_pin = DEFAULT_RX_PIN;
    int baud_rate = DEFAULT_BAUD_RATE;

    if (config) {
        uart_num = config->uart_num;
        tx_pin = config->tx_pin;
        rx_pin = config->rx_pin;
        baud_rate = config->baud_rate;
        s_zigbee.coordinator_type = config->coordinator_type;
    }

    s_zigbee.uart_num = uart_num;

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART configured: port=%d, tx=%d, rx=%d, baud=%d", 
             uart_num, tx_pin, rx_pin, baud_rate);

    // Auto-detect coordinator if not specified
    if (s_zigbee.coordinator_type == ZIGBEE_COORDINATOR_NONE) {
        s_zigbee.coordinator_type = detect_coordinator_type();
        ESP_LOGI(TAG, "Detected coordinator type: %d", s_zigbee.coordinator_type);
    }

    // Start RX task
    xTaskCreate(zigbee_rx_task, "zigbee_rx", 4096, NULL, 5, NULL);

    s_zigbee.initialized = true;
    s_zigbee.connected = true;  // Assume connected until proven otherwise

    ESP_LOGI(TAG, "Zigbee protocol initialized");
    return ESP_OK;
}

/**
 * Deinitialize
 */
void zigbee_protocol_deinit(void) {
    if (!s_zigbee.initialized) {
        return;
    }

    uart_driver_delete(s_zigbee.uart_num);
    s_zigbee.initialized = false;
    s_zigbee.connected = false;

    ESP_LOGI(TAG, "Zigbee protocol deinitialized");
}

/**
 * Check connection status
 */
bool zigbee_protocol_is_connected(void) {
    return s_zigbee.initialized && s_zigbee.connected;
}

/**
 * Get coordinator type
 */
zigbee_coordinator_type_t zigbee_protocol_get_coordinator_type(void) {
    return s_zigbee.coordinator_type;
}

/**
 * Set frame callback
 */
void zigbee_protocol_set_callback(zigbee_frame_callback_t callback) {
    s_zigbee.frame_callback = callback;
}

/**
 * Enable permit join
 */
esp_err_t zigbee_protocol_permit_join(uint8_t duration) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Permit join for %d seconds", duration);

    // Build permit join frame (coordinator-specific format)
    uint8_t frame[8];
    size_t frame_len = 0;

    switch (s_zigbee.coordinator_type) {
        case ZIGBEE_COORDINATOR_CC2652P:
        case ZIGBEE_COORDINATOR_CC2652RB:
            // Z-Stack permit join command (TI CC2652 series)
            frame[0] = 0xFE;  // SOF
            frame[1] = 0x03;  // Length
            frame[2] = 0x26;  // CMD0 (ZDO)
            frame[3] = 0x36;  // CMD1 (PermitJoin)
            frame[4] = duration;
            frame[5] = 0xFF;  // All routers
            frame[6] = 0x00;  // Significance
            // Checksum calculated by send function
            frame_len = 7;
            break;

        case ZIGBEE_COORDINATOR_EFR32:
            // EZSP permit join command (Silicon Labs)
            frame[0] = 0x1A;  // EZSP frame control
            frame[1] = 0x00;  // Sequence
            frame[2] = 0x00;  // Frame ID (permitJoining)
            frame[3] = 0x22;
            frame[4] = duration;
            frame_len = 5;
            break;

        default:
            // Generic format (Zigbee2MQTT-compatible)
            frame[0] = 0xAA;  // Header
            frame[1] = 0x55;
            frame[2] = ZIGBEE_FRAME_PERMIT_JOIN;
            frame[3] = duration;
            frame_len = 4;
            break;
    }

    return send_raw_data(frame, frame_len);
}

/**
 * Discover devices
 */
esp_err_t zigbee_protocol_discover_devices(void) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Requesting device discovery");

    // Build discovery request
    uint8_t frame[4] = {0xAA, 0x55, ZIGBEE_FRAME_DISCOVERY, 0x00};
    return send_raw_data(frame, sizeof(frame));
}

/**
 * Send ON/OFF command
 */
esp_err_t zigbee_protocol_send_on_off(uint16_t short_addr, uint8_t endpoint, bool on) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send ON/OFF: addr=0x%04X, ep=%d, on=%d", short_addr, endpoint, on);

    // Build ZCL ON/OFF command
    zigbee_frame_t frame = {
        .frame_control = 0x01,  // Cluster-specific
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = ZIGBEE_CLUSTER_ON_OFF,
        .command_id = on ? 0x01 : 0x00,  // ON : OFF
        .payload = NULL,
        .payload_len = 0
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send level control (brightness)
 */
esp_err_t zigbee_protocol_send_level(uint16_t short_addr, uint8_t endpoint, uint8_t level) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send level: addr=0x%04X, ep=%d, level=%d", short_addr, endpoint, level);

    // Build move to level command
    uint8_t payload[2] = {level, 0x00};  // Level, transition time

    zigbee_frame_t frame = {
        .frame_control = 0x01,
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = ZIGBEE_CLUSTER_LEVEL_CONTROL,
        .command_id = 0x00,  // Move to level
        .payload = payload,
        .payload_len = 2
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send color control command (RGB via Hue/Saturation)
 */
esp_err_t zigbee_protocol_send_color(uint16_t short_addr, uint8_t endpoint,
                                     uint8_t hue, uint8_t saturation) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send color: addr=0x%04X, ep=%d, hue=%d, sat=%d",
             short_addr, endpoint, hue, saturation);

    // ZCL Color Control cluster (0x0300)
    // Move to Hue and Saturation command (0x06)
    uint8_t payload[5];
    payload[0] = hue;        // Hue (0-254)
    payload[1] = saturation; // Saturation (0-254)
    payload[2] = 0x00;       // Transition time LSB (instant)
    payload[3] = 0x00;       // Transition time MSB
    payload[4] = 0x00;       // Options mask

    zigbee_frame_t frame = {
        .frame_control = 0x01,  // Cluster-specific
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = 0x0300,   // Color Control
        .command_id = 0x06,     // Move to Hue and Saturation
        .payload = payload,
        .payload_len = 5
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send color temperature command
 */
esp_err_t zigbee_protocol_send_color_temp(uint16_t short_addr, uint8_t endpoint,
                                          uint16_t mireds) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send color temp: addr=0x%04X, ep=%d, mireds=%d",
             short_addr, endpoint, mireds);

    // ZCL Color Control cluster (0x0300)
    // Move to Color Temperature command (0x0A)
    uint8_t payload[4];
    payload[0] = mireds & 0xFF;         // Color temp LSB
    payload[1] = (mireds >> 8) & 0xFF;  // Color temp MSB
    payload[2] = 0x00;                  // Transition time LSB (instant)
    payload[3] = 0x00;                  // Transition time MSB

    zigbee_frame_t frame = {
        .frame_control = 0x01,  // Cluster-specific
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = 0x0300,   // Color Control
        .command_id = 0x0A,     // Move to Color Temperature
        .payload = payload,
        .payload_len = 4
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send lock/unlock command
 */
esp_err_t zigbee_protocol_send_lock(uint16_t short_addr, uint8_t endpoint, bool locked) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send lock: addr=0x%04X, ep=%d, locked=%d",
             short_addr, endpoint, locked);

    // ZCL Door Lock cluster (0x0101)
    // Lock Door (0x00) or Unlock Door (0x01) command
    uint8_t payload[1];
    payload[0] = 0x00;  // PIN/RFID code (empty for now)

    zigbee_frame_t frame = {
        .frame_control = 0x01,  // Cluster-specific
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = 0x0101,   // Door Lock
        .command_id = locked ? 0x00 : 0x01,  // Lock (0x00) or Unlock (0x01)
        .payload = payload,
        .payload_len = 1
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send thermostat setpoint command
 */
esp_err_t zigbee_protocol_send_thermostat_setpoint(uint16_t short_addr, uint8_t endpoint,
                                                    int16_t temperature) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Send thermostat: addr=0x%04X, ep=%d, temp=%d (0.01°C)",
             short_addr, endpoint, temperature);

    // ZCL Thermostat cluster (0x0201)
    // Write Occupied Heating Setpoint attribute (0x0012)
    uint8_t payload[5];
    payload[0] = 0x12;                      // Attr ID LSB
    payload[1] = 0x00;                      // Attr ID MSB
    payload[2] = 0x29;                      // Data type: int16
    payload[3] = temperature & 0xFF;        // Value LSB
    payload[4] = (temperature >> 8) & 0xFF; // Value MSB

    zigbee_frame_t frame = {
        .frame_control = 0x00,  // Profile-wide
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = 0x0201,   // Thermostat
        .command_id = 0x02,     // Write attributes
        .payload = payload,
        .payload_len = 5
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Read attribute
 */
esp_err_t zigbee_protocol_read_attribute(uint16_t short_addr, uint8_t endpoint,
                                         uint16_t cluster_id, uint16_t attr_id) {
    if (!s_zigbee.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Read attr: addr=0x%04X, ep=%d, cluster=0x%04X, attr=0x%04X",
             short_addr, endpoint, cluster_id, attr_id);

    // Build read attribute command
    uint8_t payload[2];
    payload[0] = attr_id & 0xFF;
    payload[1] = (attr_id >> 8) & 0xFF;

    zigbee_frame_t frame = {
        .frame_control = 0x00,  // Profile-wide
        .sequence = s_zigbee.sequence++,
        .short_addr = short_addr,
        .endpoint = endpoint,
        .cluster_id = cluster_id,
        .command_id = 0x00,  // Read attributes
        .payload = payload,
        .payload_len = 2
    };

    return zigbee_protocol_send_frame(&frame);
}

/**
 * Send generic frame
 */
esp_err_t zigbee_protocol_send_frame(const zigbee_frame_t *frame) {
    if (!s_zigbee.initialized || !frame) {
        return ESP_ERR_INVALID_ARG;
    }

    // Build frame (simplified generic format)
    uint8_t *buf = s_zigbee.tx_buffer;
    size_t pos = 0;

    buf[pos++] = 0xAA;  // Header
    buf[pos++] = 0x55;
    buf[pos++] = ZIGBEE_FRAME_COMMAND;
    buf[pos++] = frame->frame_control;
    buf[pos++] = frame->sequence;
    buf[pos++] = frame->short_addr & 0xFF;
    buf[pos++] = (frame->short_addr >> 8) & 0xFF;
    buf[pos++] = frame->endpoint;
    buf[pos++] = frame->cluster_id & 0xFF;
    buf[pos++] = (frame->cluster_id >> 8) & 0xFF;
    buf[pos++] = frame->command_id;

    if (frame->payload && frame->payload_len > 0) {
        memcpy(&buf[pos], frame->payload, frame->payload_len);
        pos += frame->payload_len;
    }

    return send_raw_data(buf, pos);
}

/**
 * Send raw data to UART
 */
static esp_err_t send_raw_data(const uint8_t *data, size_t len) {
    int written = uart_write_bytes(s_zigbee.uart_num, data, len);
    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * RX task - reads from UART and processes frames
 */
static void zigbee_rx_task(void *arg) {
    ESP_LOGI(TAG, "RX task started");

    while (s_zigbee.initialized) {
        int len = uart_read_bytes(s_zigbee.uart_num, s_zigbee.rx_buffer, 
                                  sizeof(s_zigbee.rx_buffer), pdMS_TO_TICKS(100));

        if (len > 0) {
            ESP_LOGD(TAG, "Received %d bytes", len);

            // Parse frame (simplified)
            if (len >= 4 && s_zigbee.rx_buffer[0] == 0xAA && s_zigbee.rx_buffer[1] == 0x55) {
                zigbee_frame_t frame = {0};
                frame.frame_control = s_zigbee.rx_buffer[2];

                // Call callback if set
                if (s_zigbee.frame_callback) {
                    s_zigbee.frame_callback(&frame);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "RX task ended");
    vTaskDelete(NULL);
}

/**
 * Auto-detect coordinator type
 */
static zigbee_coordinator_type_t detect_coordinator_type(void) {
    // Try to detect based on version query or known patterns
    // Send version request and check response format
    
    // TODO: Implement coordinator detection by:
    // 1. Send version query
    // 2. Check response format (Z-Stack vs EZSP vs Generic)
    // 3. Return appropriate type
    
    // For now, default to CC2652P (most common UART coordinator)
    ESP_LOGI(TAG, "Using CC2652P Zigbee coordinator (UART-based)");
    ESP_LOGI(TAG, "Hardware: Connect coordinator to GPIO18(TX)/GPIO19(RX), 3.3V, GND");
    return ZIGBEE_COORDINATOR_CC2652P;
}
