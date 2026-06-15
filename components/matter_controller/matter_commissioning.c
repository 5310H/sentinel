/**
 * @file matter_commissioning.c
 * @brief Matter device commissioning and pairing implementation
 */

#include "matter_controller.h"
#include "esp_log.h"
#include "esp_matter_controller_client.h"
#include "esp_matter_controller_pairing.h"
#include <string.h>

static const char *TAG = "MATTER_PAIR";

// Forward declaration
extern esp_matter_controller_t *g_controller;

/**
 * @brief Parse Matter QR code payload
 * 
 * Format: MT:Y.K9QAO00KNY0648G00
 * or numeric: 34970112332
 */
static int _parse_pairing_code(const char *code, uint32_t *setup_pin, uint16_t *discriminator) {
    if (!code || !setup_pin || !discriminator) {
        return -1;
    }
    
    // Check if it's manual pairing code (starts with MT:)
    if (strncmp(code, "MT:", 3) == 0) {
        // Parse manual pairing code
        // Format: MT:<version>.<encoded_payload>
        ESP_LOGI(TAG, "Parsing manual pairing code");
        
        // TODO: Implement manual code parsing
        *setup_pin = 20202021;  // Default for testing
        *discriminator = 3840;
        
        return 0;
    }
    
    // Numeric QR code payload
    char *endptr;
    unsigned long long payload = strtoull(code, &endptr, 10);
    
    if (*endptr != '\0') {
        ESP_LOGE(TAG, "Invalid numeric pairing code");
        return -1;
    }
    
    // Extract setup PIN and discriminator from payload
    // TODO: Implement proper QR code decoding
    *setup_pin = (uint32_t)(payload & 0x7FFFFFF);
    *discriminator = (uint16_t)((payload >> 27) & 0xFFF);
    
    return 0;
}

/**
 * @brief Commission device using on-network discovery
 */
int matter_commission_on_network(const char *pairing_code, const char *device_name, uint64_t *out_node_id) {
    if (!g_controller || !pairing_code || !out_node_id) {
        return -1;
    }
    
    uint32_t setup_pin;
    uint16_t discriminator;
    
    if (_parse_pairing_code(pairing_code, &setup_pin, &discriminator) != 0) {
        ESP_LOGE(TAG, "Failed to parse pairing code");
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioning device: PIN=%u, Discriminator=%u", setup_pin, discriminator);
    
    // Discover device on network
    esp_matter_controller_pairing_config_t pair_config = {
        .commissioning_mode = ESP_MATTER_COMMISSIONING_ON_NETWORK,
        .setup_pin_code = setup_pin,
        .discriminator = discriminator,
        .discovery_timeout_ms = 30000,  // 30 second discovery timeout
    };
    
    esp_err_t err = esp_matter_controller_pair_device(g_controller, &pair_config, out_node_id);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pairing failed: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Device paired successfully: Node ID 0x%llX", *out_node_id);
    
    return 0;
}

/**
 * @brief Commission device using BLE (Bluetooth)
 */
int matter_commission_ble(const char *pairing_code, const char *device_name, uint64_t *out_node_id) {
    if (!g_controller || !pairing_code || !out_node_id) {
        return -1;
    }
    
    uint32_t setup_pin;
    uint16_t discriminator;
    
    if (_parse_pairing_code(pairing_code, &setup_pin, &discriminator) != 0) {
        ESP_LOGE(TAG, "Failed to parse pairing code");
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioning device via BLE: PIN=%u, Discriminator=%u", setup_pin, discriminator);
    
    // Discover device over Bluetooth
    esp_matter_controller_pairing_config_t pair_config = {
        .commissioning_mode = ESP_MATTER_COMMISSIONING_BLE,
        .setup_pin_code = setup_pin,
        .discriminator = discriminator,
        .discovery_timeout_ms = 60000,  // 60 second BLE discovery
    };
    
    esp_err_t err = esp_matter_controller_pair_device(g_controller, &pair_config, out_node_id);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE pairing failed: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Device paired via BLE: Node ID 0x%llX", *out_node_id);
    
    return 0;
}

/**
 * @brief Open commissioning window on already paired device
 * 
 * Allows additional controllers (like Home Assistant) to also control the device
 */
int matter_open_commissioning_window(uint64_t node_id, uint16_t timeout_sec, uint32_t *out_setup_pin) {
    if (!g_controller || !out_setup_pin) {
        return -1;
    }
    
    ESP_LOGI(TAG, "Opening commissioning window for node 0x%llX", node_id);
    
    // Open enhanced commissioning window
    esp_matter_controller_window_config_t window_config = {
        .node_id = node_id,
        .timeout_sec = timeout_sec,
        .iteration = 1000,  // PBKDF2 iterations
    };
    
    esp_err_t err = esp_matter_controller_open_commissioning_window(g_controller, &window_config, out_setup_pin);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open commissioning window: %d", err);
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioning window opened. Setup PIN: %u", *out_setup_pin);
    
    return 0;
}

/**
 * @brief Check if device is currently discoverable
 */
int matter_is_device_discoverable(uint16_t discriminator, bool *is_discoverable) {
    if (!g_controller || !is_discoverable) {
        return -1;
    }
    
    // Scan for devices with matching discriminator
    // TODO: Implement discovery scan
    
    *is_discoverable = false;
    return 0;
}

/**
 * @brief Get device information after commissioning
 */
int matter_get_device_info(uint64_t node_id, char *vendor_name, size_t vendor_len,
                           char *product_name, size_t product_len,
                           uint16_t *vendor_id, uint16_t *product_id) {
    if (!g_controller) {
        return -1;
    }
    
    // Read Basic Information cluster attributes
    // VendorName, ProductName, VendorID, ProductID
    
    // TODO: Implement attribute reads
    
    if (vendor_name && vendor_len > 0) {
        strncpy(vendor_name, "Unknown", vendor_len - 1);
    }
    if (product_name && product_len > 0) {
        strncpy(product_name, "Matter Device", product_len - 1);
    }
    if (vendor_id) {
        *vendor_id = 0;
    }
    if (product_id) {
        *product_id = 0;
    }
    
    return 0;
}
