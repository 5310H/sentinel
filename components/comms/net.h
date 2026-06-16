#ifndef NET_UTILS_H
#define NET_UTILS_H

/**
 * @file net.h
 * @brief Network management and HTTP communication
 * 
 * Handles:
 * - W5500 Ethernet initialization and event management
 * - MQTT connection with automatic retry/exponential backoff
 * - Unified HTTP client API (ESP32 + Linux compatible)
 * - Network status monitoring
 * - DHCP/static IP configuration
 */

#include "storage_mgr.h"

#ifdef ESP_PLATFORM
    /* ESP32 Specific Headers */
    #include "esp_http_client.h"
    #include "esp_crt_bundle.h"
    #include "esp_log.h"
    #include "esp_eth.h"
    #include "esp_netif.h"
    #include "driver/gpio.h"
    #include "driver/spi_master.h"
    #include "esp_eth_mac_w5500.h"
    #include "esp_eth_phy_w5500.h"

/**
 * @brief Apply network configuration (DHCP or static IP)
 * 
 * Configures IPv4 settings on network interface.
 * Uses storage_get_network()->use_dhcp to determine mode.
 * 
 * @param netif Network interface pointer
 */
void init_network_hardware(esp_netif_t *netif);

#else
    /* Linux Specific Headers */
    #include <curl/curl.h>    
#endif

/**
 * @brief Initialize W5500 Ethernet hardware
 * 
 * Sets up SPI2 bus and configures W5500 chip for:
 * - MAC address
 * - Interrupt GPIO (GPIO4)
 * - PHY reset GPIO (GPIO5)
 * 
 * Registers event handlers for IP_EVENT_ETH_GOT_IP.
 * Called once during network_loop_task() initialization.
 */
void init_ethernet_v3(void);

/**
 * @brief Initialize MQTT connection with automatic retry
 * 
 * Connects to configured MQTT broker with:
 * - Exponential backoff retry (5s, 15s, 30s, 60s)
 * - TLS/SSL support with certificate bundle
 * - Authentication (username/password)
 * - Keep-alive heartbeat
 * 
 * Subscribes to sentinel/alarm/set topic for remote commands.
 * Publishes status every 30 seconds.
 * 
 * Called continuously by network_loop_task().
 */
void start_sentinel_mqtt(void);

/**
 * @brief Unified HTTP request (cross-platform)
 * 
 * Sends HTTP request and handles response.
 * Automatically selects platform implementation:
 * - ESP32: esp_http_client (with TLS/certificate bundle)
 * - Linux: libcurl
 * 
 * @param url Full destination URL (http or https)
 * @param post_data JSON string for POST/PATCH requests, NULL for GET
 * @param bearer Optional Bearer token for Authorization header
 * @param method HTTP method: "GET", "POST", or "PATCH"
 */
void net_req(const char* url, const char* post_data, const char* bearer, const char* method);

/**
 * @brief Main network task (FreeRTOS)
 * 
 * Runs event loop for:
 * - Mongoose HTTP server (port 80)
 * - MQTT connection management with retry logic
 * - Status heartbeat publishing (30s interval)
 * - Network status logging (5m interval)
 * 
 * Blocks on mg_mgr_poll() with 50ms timeout.
 * 
 * @param pvParameters FreeRTOS task parameter (unused)
 */
void network_loop_task(void *pvParameters);

/**
 * @brief Global network status flag
 * 
 * Set to true when IP_EVENT_ETH_GOT_IP received.
 * Set to false on IP_EVENT_ETH_LOST_IP.
 * Used by other tasks to determine if network is available.
 */
extern bool g_network_up;

/**
 * @brief Global Mongoose manager (for HTTP/MQTT)
 * 
 * Shared across all network components for connection management.
 */
extern struct mg_mgr mgr;

#endif // NET_UTILS_H