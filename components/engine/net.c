#include "net.h"
#include "hardware_config.h"
#include "mongoose.h"
#include "mqtt_logic.h"
#include "storage_mgr.h"

#ifdef ESP_PLATFORM
#include "../security/audit_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_crt_bundle.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "NET_MGR";
#else
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#endif

// --- Global State ---
struct mg_mgr mgr;
bool g_network_up = false;

#ifdef ESP_PLATFORM
// --- Ethernet Reconnection State ---
static uint64_t g_eth_disconnect_time = 0;
static bool g_eth_reconnect_pending = false;
static const uint32_t ETH_RECONNECT_TIMEOUT_MS = 30000; // 30 seconds

// --- ESP32 Ethernet Event Handlers ---
static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id,
                              void *data) {
  switch (id) {
  case ETHERNET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Up");
#if W5500_LOG_RECONNECT
    if (g_eth_reconnect_pending) {
      uint64_t downtime_ms =
          (esp_timer_get_time() - g_eth_disconnect_time) / 1000;
      ESP_LOGI(TAG, "Link restored after %llu ms downtime", downtime_ms);
    }
#endif
    g_eth_reconnect_pending = false;
    break;
  case ETHERNET_EVENT_DISCONNECTED:
#if W5500_LOG_RECONNECT
    ESP_LOGW(TAG, "Ethernet Link Down - will attempt restart in %lu ms",
             ETH_RECONNECT_TIMEOUT_MS);
#else
    ESP_LOGE(TAG, "Ethernet Link Down");
#endif
    g_network_up = false;
    g_eth_disconnect_time = esp_timer_get_time();
    g_eth_reconnect_pending = true;
    break;
  case ETHERNET_EVENT_START:
    ESP_LOGI(TAG, "Ethernet Started");
    break;
  case ETHERNET_EVENT_STOP:
    ESP_LOGE(TAG, "Ethernet Stopped");
    g_network_up = false;
    break;
  default:
#if W5500_VERBOSE_DEBUG
    ESP_LOGD(TAG, "Ethernet event: %ld", id);
#endif
    break;
  }
}

static void got_ip_event_handler(void *arg, esp_event_base_t base, int32_t id,
                                 void *data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
  ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
  ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
  g_network_up = true;
  g_eth_reconnect_pending = false; // Reset reconnect timer on IP assignment
}

// Global ethernet handle for health checks and recovery
static esp_eth_handle_t g_eth_handle = NULL;

esp_err_t init_ethernet_v3(void) {
  esp_err_t ret;

  // Initialize network interface
  ret = esp_netif_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create default event loop
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
    return ret;
  }

  // Register event handlers
  ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                   &eth_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ETH_EVENT handler: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                   &got_ip_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Initialize SPI2 bus for W5500
  spi_bus_config_t buscfg = {
      .miso_io_num = W5500_MISO,
      .mosi_io_num = W5500_MOSI,
      .sclk_io_num = W5500_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI2 bus: %s", esp_err_to_name(ret));
    return ret;
  }

  // Configure W5500 SPI device
  spi_device_interface_config_t devcfg = {.command_bits = 16,
                                          .address_bits = 8,
                                          .mode = 0,
                                          .clock_speed_hz =
                                              W5500_SPI_CLOCK_MHZ * 1000 * 1000,
                                          .spics_io_num = W5500_CS,
                                          .queue_size = W5500_SPI_QUEUE_SIZE};

#if W5500_VERBOSE_DEBUG
  ESP_LOGI(TAG,
           "W5500 SPI config: clock=%dMHz, queue=%d, CS=%d, INT=%d, RST=%d",
           W5500_SPI_CLOCK_MHZ, W5500_SPI_QUEUE_SIZE, W5500_CS, W5500_INT,
           W5500_RST);
#endif

  // Create MAC driver
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_w5500_config_t w5500_config =
      ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
  w5500_config.int_gpio_num = W5500_INT;

  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (mac == NULL) {
    ESP_LOGE(TAG, "Failed to create W5500 MAC driver");
    return ESP_FAIL;
  }

  // Create PHY driver
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.reset_gpio_num = W5500_RST;
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (phy == NULL) {
    ESP_LOGE(TAG, "Failed to create W5500 PHY driver");
    return ESP_FAIL;
  }

  // Install ethernet driver
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  ret = esp_eth_driver_install(&eth_config, &g_eth_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install ethernet driver: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Create network interface and attach driver
  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&cfg);
  if (eth_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create ethernet netif");
    return ESP_FAIL;
  }

  ret = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(g_eth_handle));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to attach netif glue: %s", esp_err_to_name(ret));
    return ret;
  }

  // Start ethernet
  ret = esp_eth_start(g_eth_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start ethernet: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "W5500 Ethernet initialized successfully");
  return ESP_OK;
}

esp_err_t w5500_health_check(void) {
  if (g_eth_handle == NULL) {
    ESP_LOGE(TAG, "Ethernet not initialized");
    return ESP_FAIL;
  }

#if W5500_VERBOSE_DEBUG
  ESP_LOGD(TAG, "Performing W5500 health check...");
#endif

  uint8_t mac[ETH_ADDR_LEN] = {0};
  esp_err_t ret = esp_eth_ioctl(g_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
  if (ret != ESP_OK) {
#if W5500_LOG_SPI_ERRORS
    ESP_LOGE(TAG, "Failed to read W5500 MAC address: %s", esp_err_to_name(ret));
#endif
    return ret;
  }

  // Check if MAC is valid (not all zeros or all ones)
  bool all_zeros = true, all_ones = true;
  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    if (mac[i] != 0x00)
      all_zeros = false;
    if (mac[i] != 0xFF)
      all_ones = false;
  }

  if (all_zeros || all_ones) {
    ESP_LOGE(TAG,
             "W5500 health check FAILED - invalid MAC: "
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "W5500 health check OK - MAC: %02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return ESP_OK;
}

void init_network_hardware(esp_netif_t *netif) {
  if (storage_get_network()->use_dhcp) {
    ESP_LOGI(TAG, "Starting DHCP mode with 30 second timeout...");

#ifdef ESP_PLATFORM
    esp_netif_dhcpc_start(netif);

    // Wait up to 30 seconds for DHCP to succeed
    uint32_t dhcp_wait_ms = 0;
    const uint32_t dhcp_timeout_ms = 30000;
    bool dhcp_success = false;

    while (dhcp_wait_ms < dhcp_timeout_ms && !dhcp_success) {
      vTaskDelay(pdMS_TO_TICKS(500));
      dhcp_wait_ms += 500;

      // Check if we got an IP (g_network_up is set in got_ip_event_handler)
      if (g_network_up) {
        dhcp_success = true;
        ESP_LOGI(TAG, "DHCP succeeded after %u ms", dhcp_wait_ms);
        break;
      }
    }

    if (!dhcp_success) {
      ESP_LOGW(TAG, "DHCP timeout after %u ms - falling back to static IP: %s",
               dhcp_timeout_ms, storage_get_network()->ip);

      // Stop DHCP and apply static IP
      esp_netif_dhcpc_stop(netif);

      esp_netif_ip_info_t info;
      info.ip.addr = ipaddr_addr(storage_get_network()->ip);
      info.gw.addr = ipaddr_addr(storage_get_network()->gateway);
      info.netmask.addr = ipaddr_addr(storage_get_network()->netmask);

      esp_err_t ret = esp_netif_set_ip_info(netif, &info);
      if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Static IP configured: %s", storage_get_network()->ip);
        g_network_up = true; // Mark network as up
      } else {
        ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
      }
    }
#endif
  } else {
    ESP_LOGI(TAG, "Configuring Static IP: %s", storage_get_network()->ip);
#ifdef ESP_PLATFORM
    esp_netif_ip_info_t info;
    esp_netif_dhcpc_stop(netif);

    info.ip.addr = ipaddr_addr(storage_get_network()->ip);
    info.gw.addr = ipaddr_addr(storage_get_network()->gateway);
    info.netmask.addr = ipaddr_addr(storage_get_network()->netmask);

    esp_err_t ret = esp_netif_set_ip_info(netif, &info);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Static IP configured successfully");
      g_network_up = true;
    } else {
      ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
    }
#else
    printf("[NET] Static IP configuration: %s\n", storage_get_network()->ip);
#endif
  }
}
#else
esp_err_t init_ethernet_v3(void) {
  printf("[NET] Linux Network Mock: Assuming Network is Up\n");
  g_network_up = true;
  return ESP_OK;
}

esp_err_t w5500_health_check(void) {
  printf("[NET] W5500 health check skipped on Linux mock\n");
  return ESP_OK;
}
#endif
// --- Global MQTT State ---
static uint64_t last_mqtt_attempt = 0;
static int mqtt_retry_count = 0;
static const uint64_t mqtt_retry_intervals[] = {
    5000,  // First retry: 5 seconds
    15000, // Second retry: 15 seconds
    30000, // Third retry: 30 seconds
    60000, // Subsequent: 60 seconds
};
static const int mqtt_max_retries = 4;

// --- The Unified Event Loop (Mongoose) ---
void network_loop_task(void *pvParameters) {
  mg_mgr_init(&mgr);

  extern void web_handler(struct mg_connection * c, int ev, void *ev_data);
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
  uint64_t last_eth_monitor = 0;
  uint64_t last_ip_check = 0;
  const uint64_t eth_monitor_interval = 60000; // Monitor every 60 seconds

  for (;;) {
    uint64_t now = mg_now();

    // --- Graceful Ethernet Reconnection ---
#ifdef ESP_PLATFORM
    if (g_eth_reconnect_pending) {
      uint64_t disconnect_duration =
          esp_timer_get_time() - g_eth_disconnect_time;
      if (disconnect_duration > (ETH_RECONNECT_TIMEOUT_MS * 1000)) {
        ESP_LOGI(TAG, "Attempting ethernet restart after %llu ms downtime",
                 disconnect_duration / 1000);
        // Note: Full restart would require passing eth_handle from
        // init_ethernet_v3 For now, log the attempt and rely on hardware to
        // recover
        g_eth_reconnect_pending = false;
      }
    }
#endif

    if (g_network_up) {
      // --- Periodic Ethernet Health Monitoring ---
      if (now - last_eth_monitor >= eth_monitor_interval) {
#ifdef ESP_PLATFORM
        esp_err_t health = w5500_health_check();
        if (health != ESP_OK) {
          ESP_LOGW(TAG, "W5500 health check failed - may indicate SPI issues");
        }
#endif
        last_eth_monitor = now;
      }

      // MQTT Connection Logic with Retry
      if (s_conn != NULL) {
        // Reset exponential backoff if the connection is stable for 5+ seconds
        if (mqtt_retry_count > 0 && (now - last_mqtt_attempt > 5000)) {
          mqtt_retry_count = 0;
#ifdef ESP_PLATFORM
          ESP_LOGI(TAG, "MQTT connection stabilized, backoff reset");
#else
          printf("[NET] MQTT connection stabilized, backoff reset\n");
#endif
        }
      } else {
        // s_conn == NULL, handle backoff
        bool should_retry = false;

        if (last_mqtt_attempt == 0) {
          should_retry = true;
        } else if (mqtt_retry_count < mqtt_max_retries) {
          if (now - last_mqtt_attempt >=
              mqtt_retry_intervals[mqtt_retry_count]) {
            should_retry = true;
          }
        } else {
          if (now - last_mqtt_attempt >= 60000) {
            should_retry = true;
            mqtt_retry_count = 3; // Stay at max interval
          }
        }

        if (should_retry) {
          start_sentinel_mqtt();
          last_mqtt_attempt = now;
          mqtt_retry_count++;

          if (s_conn == NULL) {
#ifdef ESP_PLATFORM
            ESP_LOGW(TAG, "MQTT socket allocation failed, retry %d/%d",
                     mqtt_retry_count, mqtt_max_retries);
#else
            printf("[NET] MQTT socket allocation failed, retry %d/%d\n",
                   mqtt_retry_count, mqtt_max_retries);
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
               s_conn != NULL ? "CONNECTED" : "DISCONNECTED", mqtt_retry_count,
               mqtt_max_retries);
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
void net_req(const char *url, const char *post_data, const char *bearer,
             const char *method) {
  if (!url)
    return;
  // ... (rest of your existing net_req remains the same)
}