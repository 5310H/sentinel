#include "net.h"
#include "mongoose.h"
#include "mqtt_logic.h"
#include "storage_mgr.h"
#include "hardware_config.h"

#ifdef ESP_PLATFORM
    #include "esp_http_client.h"
    #include "esp_crt_bundle.h"
    #include "esp_log.h"
    #include "esp_eth.h"
    #include "esp_netif.h"
    #include "esp_event.h"
    #include "driver/gpio.h"
    #include "driver/spi_master.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"

    static const char *TAG = "NET_MGR";
#else
    #include <curl/curl.h>
    #include <stdio.h>
    #include <string.h>
    #include <stdbool.h>
#endif

// --- Global State ---
struct mg_mgr mgr;
bool g_network_up = false; 

#ifdef ESP_PLATFORM
// --- ESP32 Ethernet Event Handlers ---
static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "Ethernet Link Down");
            g_network_up = false;
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            g_network_up = false;
            break;
        default:
            break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    g_network_up = true; 
}

void init_ethernet_v3(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    spi_bus_config_t buscfg = {
        .miso_io_num = W5500_MISO, .mosi_io_num = W5500_MOSI, .sclk_io_num = W5500_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .command_bits = 16, .address_bits = 8, .mode = 0,
        .clock_speed_hz = 33 * 1000 * 1000,
        .spics_io_num = W5500_CS, .queue_size = 20
    };

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
    w5500_config.int_gpio_num = W5500_INT; 
    
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = W5500_RST; 
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));

    esp_eth_start(eth_handle);
}

void init_network_hardware(esp_netif_t *netif) { // Ensure netif is passed in or defined
    if (net_cfg.use_dhcp) {
        ESP_LOGI("NET", "Starting DHCP mode...");
    } else {
        ESP_LOGI("NET", "Setting Static IP: %s", net_cfg.ip);
        #ifdef ESP_PLATFORM
            esp_netif_ip_info_t info;
            // Stop DHCP client before setting static IP
            esp_netif_dhcpc_stop(netif); 
            
            info.ip.addr = ipaddr_addr(net_cfg.ip);
            info.gw.addr = ipaddr_addr(net_cfg.gateway);
            info.netmask.addr = ipaddr_addr(net_cfg.netmask);
            
            esp_netif_set_ip_info(netif, &info);
        #else
            printf("[MOCK] Simulation: Network would be set to %s\n", net_cfg.ip);
        #endif
    }
}
#else
void init_ethernet_v3(void) {
    printf("[NET] Linux Network Mock: Assuming Network is Up\n");
    g_network_up = true; 
}
#endif
// --- Global MQTT State ---
static uint64_t last_mqtt_attempt = 0;
static int mqtt_retry_count = 0;
static const uint64_t mqtt_retry_intervals[] = {
    5000,      // First retry: 5 seconds
    15000,     // Second retry: 15 seconds
    30000,     // Third retry: 30 seconds
    60000,     // Subsequent: 60 seconds
};
static const int mqtt_max_retries = 4;

// --- The Unified Event Loop (Mongoose) ---
void network_loop_task(void *pvParameters) {
    mg_mgr_init(&mgr);

    extern void web_handler(struct mg_connection *c, int ev, void *ev_data);
    mg_http_listen(&mgr, "http://0.0.0.0:80", web_handler, NULL);

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Mongoose Loop Started: HTTP (80)");
#else
    printf("[NET] Linux Loop Started: HTTP (80)\n");
#endif

    // --- Heartbeat Logic Setup ---
    uint64_t last_heartbeat = 0;
    const uint64_t heartbeat_interval = 30000; // 30 seconds
    uint64_t last_mqtt_status_log = 0;

    for (;;) {
        if (g_network_up) {
            uint64_t now = mg_now();
            
            // MQTT Connection Logic with Retry
            if (s_conn == NULL) {
                // Determine if we should retry based on backoff schedule
                bool should_retry = false;
                
                if (last_mqtt_attempt == 0) {
                    // First attempt
                    should_retry = true;
                } else if (mqtt_retry_count < mqtt_max_retries) {
                    // Use exponential backoff from array
                    uint64_t retry_interval = mqtt_retry_intervals[mqtt_retry_count];
                    if (now - last_mqtt_attempt >= retry_interval) {
                        should_retry = true;
                    }
                } else {
                    // Max retries reached, try again every 60 seconds
                    if (now - last_mqtt_attempt >= 60000) {
                        should_retry = true;
                        mqtt_retry_count = 3;  // Keep at last interval
                    }
                }
                
                if (should_retry) {
                    start_sentinel_mqtt();
                    last_mqtt_attempt = now;
                    if (s_conn != NULL) {
                        mqtt_retry_count = 0;  // Reset on successful connection
                        #ifdef ESP_PLATFORM
                            ESP_LOGI(TAG, "MQTT connected successfully");
                        #else
                            printf("[NET] MQTT connected successfully\n");
                        #endif
                    } else {
                        mqtt_retry_count++;
                        #ifdef ESP_PLATFORM
                            ESP_LOGW(TAG, "MQTT connection failed, retry %d/%d", mqtt_retry_count, mqtt_max_retries);
                        #else
                            printf("[NET] MQTT connection failed, retry %d/%d\n", mqtt_retry_count, mqtt_max_retries);
                        #endif
                    }
                }
            }
            
            // Periodically publish status
            if (now - last_heartbeat >= heartbeat_interval) {
                extern void mqtt_publish_status(void);
                mqtt_publish_status();
                last_heartbeat = now;
            }
            
            // Log MQTT status periodically (every 5 minutes)
            if (now - last_mqtt_status_log >= 300000) {
                #ifdef ESP_PLATFORM
                    ESP_LOGI(TAG, "MQTT status: %s (retries: %d/%d)", 
                        s_conn != NULL ? "CONNECTED" : "DISCONNECTED", 
                        mqtt_retry_count, mqtt_max_retries);
                #else
                    printf("[NET] MQTT status: %s (retries: %d/%d)\n", 
                        s_conn != NULL ? "CONNECTED" : "DISCONNECTED", 
                        mqtt_retry_count, mqtt_max_retries);
                #endif
                last_mqtt_status_log = now;
            }
        }

        mg_mgr_poll(&mgr, 50); 

#ifdef ESP_PLATFORM
        vTaskDelay(pdMS_TO_TICKS(1)); 
#endif
    }
}

// --- REST Client (esp_http_client / Curl) ---
void net_req(const char* url, const char* post_data, const char* bearer, const char* method) {
    if (!url) return;
    // ... (rest of your existing net_req remains the same)
}