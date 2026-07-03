#include "network_watchdog.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <stdio.h>
#endif
#include "storage_mgr.h"
#include <string.h>

static net_watchdog_cb_t s_state_cb = NULL;
static net_watchdog_state_t s_current_state = NET_WATCHDOG_STATE_UNKNOWN;
static bool s_running = false;

#ifdef ESP_PLATFORM
static const char *TAG = "NET_WATCHDOG";
static TaskHandle_t s_watchdog_task_handle = NULL;
static uint32_t s_interval_minutes = 15;

// Helper to perform a TCP connect test to a specific IP and port
static bool check_tcp_port(const char *ip, uint16_t port) {
    if (!ip || strlen(ip) == 0) return false;

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) <= 0) {
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return false;
    }

    // Set non-blocking socket
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (res < 0 && errno == EINPROGRESS) {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);

        struct timeval tv;
        tv.tv_sec = 2; // 2 seconds timeout
        tv.tv_usec = 0;

        res = select(sock + 1, NULL, &fdset, NULL, &tv);
        if (res == 1) {
            int so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                close(sock);
                return true; // Success
            }
        }
    } else if (res == 0) {
        close(sock);
        return true; // Success immediately
    }

    close(sock);
    return false;
}

static void network_watchdog_task(void *arg) {
    ESP_LOGI(TAG, "Network watchdog task started. Interval: %lu mins", s_interval_minutes);

    while (s_running) {
        // Wait for the interval (convert mins to ms)
        // Wait in small chunks to allow for clean shutdown
        for (uint32_t i = 0; i < (s_interval_minutes * 60); i++) {
            if (!s_running) break;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!s_running) break;

        ESP_LOGD(TAG, "Performing network check...");

        bool local_up = false;
        bool ext1_up = false;
        bool ext2_up = false;

        // 1. Check local gateway (Port 53 DNS or 80 WebUI)
        storage_lock();
        char gateway[STR_SMALL];
        strncpy(gateway, storage_get_network()->gateway, sizeof(gateway));
        storage_unlock();

        if (strlen(gateway) > 0) {
            // Most routers have a DNS forwarder on port 53, or a web UI on 80
            local_up = check_tcp_port(gateway, 53) || check_tcp_port(gateway, 80);
        }

        // 2. Primary external (Cloudflare DNS - port 53)
        ext1_up = check_tcp_port("1.1.1.1", 53);

        // 3. Backup external (Google DNS - port 53)
        ext2_up = check_tcp_port("8.8.8.8", 53);

        net_watchdog_state_t new_state = NET_WATCHDOG_STATE_UNKNOWN;

        if (ext1_up || ext2_up) {
            new_state = NET_WATCHDOG_STATE_ONLINE;
            local_up = true; // If external is up, local must be up
        } else if (local_up) {
            new_state = NET_WATCHDOG_STATE_LOCAL_ONLY;
        } else {
            new_state = NET_WATCHDOG_STATE_OFFLINE;
        }

        ESP_LOGI(TAG, "Watchdog result - Local: %s, Ext1: %s, Ext2: %s -> State: %d", 
                 local_up ? "UP" : "DOWN", 
                 ext1_up ? "UP" : "DOWN", 
                 ext2_up ? "UP" : "DOWN", 
                 new_state);

        // Handle state change
        if (new_state != s_current_state) {
            ESP_LOGW(TAG, "Network state changed: %d -> %d", s_current_state, new_state);
            s_current_state = new_state;

            if (s_state_cb) {
                s_state_cb(s_current_state);
            }
        }
    }

    ESP_LOGI(TAG, "Network watchdog task terminating");
    s_watchdog_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t network_watchdog_start(uint32_t interval_minutes, net_watchdog_cb_t callback) {
    if (s_running) {
        ESP_LOGW(TAG, "Watchdog already running");
        return ESP_OK;
    }

    if (interval_minutes > 0) {
        s_interval_minutes = interval_minutes;
    }
    
    s_state_cb = callback;
    s_running = true;

    BaseType_t res = xTaskCreate(network_watchdog_task, "net_watchdog", 4096, NULL, 5, &s_watchdog_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create watchdog task");
        s_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t network_watchdog_stop(void) {
    s_running = false;
    return ESP_OK;
}

net_watchdog_state_t network_watchdog_get_state(void) {
    return s_current_state;
}

#else

// Mock implementation for Linux tests
esp_err_t network_watchdog_start(uint32_t interval_minutes, net_watchdog_cb_t callback) {
    printf("[NET_WATCHDOG] Mock start with %u min interval\n", interval_minutes);
    s_state_cb = callback;
    s_running = true;
    s_current_state = NET_WATCHDOG_STATE_ONLINE;
    return ESP_OK;
}

esp_err_t network_watchdog_stop(void) {
    s_running = false;
    return ESP_OK;
}

net_watchdog_state_t network_watchdog_get_state(void) {
    return s_current_state;
}

#endif
