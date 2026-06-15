#include "hal.h"
#include <stdio.h>
#include <stdbool.h>
#include "storage_mgr.h"    // To access net_cfg
#include <stdlib.h>    // Fixes 'free' warning
#include "hal_esp32.h" // Fixes 'hal_get_relay_state', 'hal_get_zone_state', etc.
#ifdef ESP_PLATFORM
    #include "driver/gpio.h"
    #include "esp_log.h"
    #include "esp_netif.h"      // Fixed: Added # and put inside ESP_PLATFORM
    #include "lwip/ip_addr.h"
    static const char *TAG = "HAL";
#else
    #define TAG "MOCK_HAL"
    #define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
    #define ESP_LOGE(tag, fmt, ...) printf("[" tag "] ERROR: " fmt "\n", ##__VA_ARGS__)
    #define ESP_LOGD(tag, fmt, ...) printf("[" tag "] DEBUG: " fmt "\n", ##__VA_ARGS__)
    // Mock types for Linux compatibility
    typedef void* esp_netif_t;
#endif

// KC868-A8 relay pins
static const int RELAY_PINS[] = {2, 4, 5, 13, 15, 18, 19, 21}; 

/**
 * Applies static IP settings from net_cfg to the provided interface.
 * This is now correctly wrapped in a function.
 */
void hal_net_apply_static_ip(esp_netif_t *netif) {
#ifdef ESP_PLATFORM
    if (netif == NULL) {
        ESP_LOGE(TAG, "Network interface is NULL");
        return;
    }

    if (!net_cfg.use_dhcp) {
        ESP_LOGI(TAG, "Configuring Static IP: %s", net_cfg.ip);

        // 1. Stop DHCP client
        esp_netif_dhcpc_stop(netif);

        // 2. Set IP info
        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr = ipaddr_addr(net_cfg.ip);
        ip_info.gw.addr = ipaddr_addr(net_cfg.gateway);
        ip_info.netmask.addr = ipaddr_addr(net_cfg.netmask);

        // 3. Apply
        esp_netif_set_ip_info(netif, &ip_info);
    } else {
        ESP_LOGI(TAG, "Network configured for DHCP");
    }
#else
    ESP_LOGI(TAG, "[MOCK] Static IP would be: %s", net_cfg.ip);
#endif
}

void hal_esp32_init() {
    ESP_LOGI(TAG, "Hardware Layer Initialized.");
}

void hal_init_zones(zone_t *zones, int count) {
#ifdef ESP_PLATFORM
    for (int i = 0; i < count; i++) {
        if (zones[i].gpio < 0) continue;

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << zones[i].gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE
        };
        
        gpio_config(&io_conf);
        ESP_LOGI(TAG, "Zone %d configured on GPIO %d", i, zones[i].gpio);
    }
#endif
}

int hal_get_zone_state(int pin) {
#ifdef ESP_PLATFORM
    // Validate pin before reading
    if (pin < 0 || pin > 49) {
        ESP_LOGE(TAG, "Invalid pin %d for zone read", pin);
        return 1;  // Default to safe (zone secure)
    }
    return (gpio_get_level(pin) == 0) ? 1 : 0;
#else
    return 0; 
#endif
}

void hal_set_relay(int index, int state) {
    if (index < 0 || index > 7) {
        ESP_LOGE(TAG, "Invalid relay index %d", index);
        return;
    }
    
#ifdef ESP_PLATFORM
    int gpio_pin = RELAY_PINS[index];
    // Validate GPIO
    if (gpio_pin < 0 || gpio_pin > 49) {
        ESP_LOGE(TAG, "Invalid GPIO %d for relay %d", gpio_pin, index);
        return;
    }
    gpio_set_direction(gpio_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_pin, state);
#endif
    ESP_LOGD(TAG, "Relay %d set to %s", index, state ? "ON" : "OFF");
}

void hal_set_siren(int state) { hal_set_relay(0, state); }
void hal_set_strobe(int state) { hal_set_relay(1, state); }