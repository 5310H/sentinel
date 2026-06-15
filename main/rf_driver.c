#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_rx.h"
#include "rf_driver.h"

static const char *TAG = "RF";

// --- Configuration ---
#define RF_RECEIVER_GPIO         4 // The GPIO pin connected to the RF receiver's data output
#define RMT_RESOLUTION_HZ        1000000 // 1MHz resolution, 1 tick = 1us
#define RMT_MEM_BLOCK_SYMBOLS    64      // Memory block size for RMT symbols

// --- RF Protocol Timings (example for PT2262/EV1527) ---
// All values in microseconds (us)
#define RF_TOLERANCE_US          200     // Timing tolerance for pulses
#define RF_SYNC_HIGH_US          300     // Sync pulse high duration
#define RF_SYNC_LOW_US           9300    // Sync pulse low duration
#define RF_BIT_SHORT_US          300     // Short pulse duration for data bits
#define RF_BIT_LONG_US           900     // Long pulse duration for data bits
#define RF_CODE_BIT_COUNT        24      // Number of data bits in a code

// --- Globals ---
uint32_t last_rf_code = 0;
static QueueHandle_t rmt_queue = NULL;
static rmt_channel_handle_t rx_channel = NULL;

// --- Helper Macros for timing checks ---
#define IS_WITHIN(val, target, tol) ((val) > ((target) - (tol)) && (val) < ((target) + (tol)))

/**
 * @brief RMT RX Done Callback (ISR)
 * This function is called by the RMT driver's ISR when a block of data is received.
 * It sends the received data to a queue for processing in a task.
 */
static IRAM_ATTR bool rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(rmt_queue, edata, &task_woken);
    return task_woken == pdTRUE;
}

/**
 * @brief Parses RMT symbols from the queue to decode an RF code.
 */
static void rf_parser_task(void *pvParameters) {
    rmt_rx_done_event_data_t rx_data;
    // Buffer to hold RMT symbols, owned by the RMT driver
    rmt_symbol_word_t raw_symbols[RMT_MEM_BLOCK_SYMBOLS];
    
    // Start the RMT receiver
    ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), NULL));

    while (1) {
        // Wait for the next batch of RMT symbols from the ISR
        if (xQueueReceive(rmt_queue, &rx_data, portMAX_DELAY) == pdPASS) {
            uint32_t decoded_code = 0;
            int bit_count = 0;
            bool in_sync = false;

            // Iterate through the received symbols to find a valid code
            for (size_t i = 0; i < rx_data.num_symbols; i++) {
                // Look for the sync pulse (short high, long low)
                if (!in_sync && i + 1 < rx_data.num_symbols) {
                    if (rx_data.received_symbols[i].level0 == 1 && IS_WITHIN(rx_data.received_symbols[i].duration0, RF_SYNC_HIGH_US, RF_TOLERANCE_US) &&
                        rx_data.received_symbols[i+1].level0 == 0 && IS_WITHIN(rx_data.received_symbols[i+1].duration0, RF_SYNC_LOW_US, RF_TOLERANCE_US)) {
                        in_sync = true;
                        bit_count = 0;
                        decoded_code = 0;
                        i++; // Skip the next symbol as it's part of the sync pulse
                        continue;
                    }
                }

                // If we are synced, start decoding data bits
                if (in_sync && i + 1 < rx_data.num_symbols) {
                    // Decode '0' bit (short high, long low)
                    if (rx_data.received_symbols[i].level0 == 1 && IS_WITHIN(rx_data.received_symbols[i].duration0, RF_BIT_SHORT_US, RF_TOLERANCE_US) &&
                        rx_data.received_symbols[i+1].level0 == 0 && IS_WITHIN(rx_data.received_symbols[i+1].duration0, RF_BIT_LONG_US, RF_TOLERANCE_US)) {
                        decoded_code = (decoded_code << 1) | 0;
                        bit_count++;
                        i++; // Skip next symbol
                    }
                    // Decode '1' bit (long high, short low)
                    else if (rx_data.received_symbols[i].level0 == 1 && IS_WITHIN(rx_data.received_symbols[i].duration0, RF_BIT_LONG_US, RF_TOLERANCE_US) &&
                             rx_data.received_symbols[i+1].level0 == 0 && IS_WITHIN(rx_data.received_symbols[i+1].duration0, RF_BIT_SHORT_US, RF_TOLERANCE_US)) {
                        decoded_code = (decoded_code << 1) | 1;
                        bit_count++;
                        i++; // Skip next symbol
                    } else { // If pulse doesn't match, we lost sync
                        in_sync = false;
                    }

                    // If we have a full code, process it
                    if (bit_count == RF_CODE_BIT_COUNT) {
                        last_rf_code = decoded_code;
                        ESP_LOGI(TAG, "Received RF Code: 0x%06lX", (unsigned long)last_rf_code);
                        in_sync = false; // Look for the next sync
                    }
                }
            }
            // Re-enable the receiver for the next transmission
            ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), NULL));
        }
    }
}

void rf_driver_init(void) {
    ESP_LOGI(TAG, "Initializing RMT for RF Receiver on GPIO %d", RF_RECEIVER_GPIO);

    rmt_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .gpio_num = RF_RECEIVER_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_channel));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_callback };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));
    xTaskCreate(rf_parser_task, "rf_parser_task", 3072, NULL, 10, NULL);
    ESP_LOGI(TAG, "RF Driver Initialized");
}
