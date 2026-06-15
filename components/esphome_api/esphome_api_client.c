/**
 * @file esphome_api_client.c
 * @brief ESPHome Native API client implementation
 */

#include "esphome_api_client.h"
#include "esphome_api_protocol.h"
#include <string.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
static const char *TAG = "ESPHOME_API";
#else
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sodium.h>  // For Noise protocol cryptography
#include <openssl/evp.h>
#define TAG "ESPHOME_API"
#endif

#include "../storage/storage_mgr.h"  // For esphome_devices[] array

// Use global device registry from storage_mgr.h (esphome_devices[] and esphome_count)

// Global entity registry
static esphome_entity_t g_entities[MAX_ESPHOME_DEVICES * 10];  // Up to 10 entities per device
static int g_entity_count = 0;

#ifdef ESP_PLATFORM
static SemaphoreHandle_t g_entity_mutex = NULL;
#else
static pthread_mutex_t g_entity_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// Cross-platform mutex helpers
static inline void lock_entity_mutex(void) {
#ifdef ESP_PLATFORM
    xSemaphoreTake(g_entity_mutex, portMAX_DELAY);
#else
    pthread_mutex_lock(&g_entity_mutex);
#endif
}

static inline void unlock_entity_mutex(void) {
#ifdef ESP_PLATFORM
    xSemaphoreGive(g_entity_mutex);
#else
    pthread_mutex_unlock(&g_entity_mutex);
#endif

}

/**
 * @brief Simple base64 decode function
 */
static int base64_decode(const char *input, unsigned char *output, size_t output_len) {
    static const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j, k = 0;
    unsigned char char_array_4[4], char_array_3[3];
    
    for (i = 0, j = 0; input[i] != '\0' && input[i] != '='; i++) {
        if (input[i] == ' ') continue;
        
        char_array_4[j++] = input[i];
        if (j == 4) {
            for (int m = 0; m < 4; m++) {
                char_array_4[m] = strchr(base64_chars, char_array_4[m]) - base64_chars;
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (int m = 0; m < 3 && k < output_len; m++) {
                output[k++] = char_array_3[m];
            }
            j = 0;
        }
    }
    
    return k;
}

/**
 * @brief Initialize ESPHome API client
 */
int esphome_api_init(void) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Initializing ESPHome API client...");
#else
    printf("[%s] Initializing ESPHome API client...\n", TAG);
    
    // Initialize libsodium for Noise protocol
    if (sodium_init() < 0) {
        printf("[%s] ERROR: Failed to initialize libsodium\n", TAG);
        return -1;
    }
#endif
    
    // Create entity mutex
#ifdef ESP_PLATFORM
    if (g_entity_mutex == NULL) {
        g_entity_mutex = xSemaphoreCreateMutex();
        if (g_entity_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create entity mutex");
            return -1;
        }
    }
#endif
    
    // Initialize runtime state for devices
    for (int i = 0; i < esphome_count; i++) {
        esphome_devices[i].socket_fd = -1;
        esphome_devices[i].is_connected = false;
        esphome_devices[i].use_encryption = (strlen(esphome_devices[i].encryption_key) > 0);
    }
    
    // Clear entity registry
    memset(g_entities, 0, sizeof(g_entities));
    g_entity_count = 0;
    
    // Discover entities from real devices (for physical device support)
    esphome_api_discover_all_entities();
    
    // Fallback: Create entities from device configuration for backward compatibility
    for (int i = 0; i < esphome_count; i++) {
        if (esphome_devices[i].enabled && esphome_devices[i].entity_key != 0) {
            // Only add if not already discovered
            bool already_exists = false;
            for (int k = 0; k < g_entity_count; k++) {
                if (strcmp(g_entities[k].device_hostname, esphome_devices[i].hostname) == 0 &&
                    g_entities[k].entity_key == esphome_devices[i].entity_key) {
                    already_exists = true;
                    break;
                }
            }
            
            if (!already_exists && g_entity_count < MAX_ESPHOME_DEVICES * 10) {
                // Create entity for relay control
                esphome_entity_t *entity = &g_entities[g_entity_count++];
                entity->entity_key = esphome_devices[i].entity_key;
                strcpy(entity->device_hostname, esphome_devices[i].hostname);
                strcpy(entity->object_id, "switch");
                strcpy(entity->name, esphome_devices[i].friendly_name);
                entity->type = ESPHOME_ENTITY_SWITCH;
                entity->current_state = false;
                entity->virtual_relay_id = esphome_devices[i].virtual_relay_start;
                
#ifdef ESP_PLATFORM
                ESP_LOGI(TAG, "Created fallback entity mapping: %s (key=%" PRIu32 ") -> relay %d", 
                        entity->name, entity->entity_key, entity->virtual_relay_id);
#else
                printf("[%s] Created fallback entity mapping: %s (key=%" PRIu32 ") -> relay %d\n", 
                      TAG, entity->name, entity->entity_key, entity->virtual_relay_id);
#endif
            }
        }
    }
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "ESPHome API client initialized successfully");
    ESP_LOGI(TAG, "Loaded %d ESPHome devices from esphome.json", esphome_count);
    
    // Auto-connect to enabled devices
    for (int i = 0; i < esphome_count; i++) {
        if (esphome_devices[i].enabled) {
            ESP_LOGI(TAG, "Auto-connecting to %s (%s)...", 
                esphome_devices[i].friendly_name, esphome_devices[i].hostname);
            esphome_api_connect(esphome_devices[i].hostname, 
                              esphome_devices[i].port,
                              esphome_devices[i].password,
                              esphome_devices[i].encryption_key);
        }
    }
#else
    printf("[%s] ESPHome API client initialized successfully\n", TAG);
    printf("[%s] Loaded %d ESPHome devices from esphome.json\n", TAG, esphome_count);
    
    // Note: Auto-connect disabled on Linux (would be called from zone monitoring)
#endif
    
    return 0;
}

/**
 * @brief Find device by hostname
 */
static esphome_device_t* _find_device(const char *hostname) {
    for (int i = 0; i < esphome_count; i++) {
        if (strcmp(esphome_devices[i].hostname, hostname) == 0) {
            return &esphome_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief Connect to ESPHome device
 */
int esphome_api_connect(const char *hostname, uint16_t port, 
                        const char *password, const char *encryption_key) {
    if (!hostname) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Invalid hostname");
#else
        printf("[%s] Invalid hostname\n", TAG);
#endif
        return -1;
    }
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Connecting to %s:%d...", hostname, port);
#else
    printf("[%s] Connecting to %s:%d...\n", TAG, hostname, port);
#endif
    
    // Find device in configuration
    esphome_device_t *dev = _find_device(hostname);
    if (!dev) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Device %s not found in configuration", hostname);
#else
        printf("[%s] Device %s not found in configuration\n", TAG, hostname);
#endif
        return -1;
    }
    
    if (dev->is_connected) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Already connected to %s", hostname);
#else
        printf("[%s] Already connected to %s\n", TAG, hostname);
#endif
        return 0;
    }
    
    // Store encryption key if provided (override config)
    if (encryption_key && strlen(encryption_key) > 0) {
        size_t key_len = strlen(encryption_key);
        if (key_len >= sizeof(dev->encryption_key)) {
#ifdef ESP_PLATFORM
            ESP_LOGW(TAG, "Encryption key truncated (max %zu bytes)", sizeof(dev->encryption_key) - 1);
#else
            printf("[%s] WARNING: Encryption key truncated (max %zu bytes)\n", TAG, sizeof(dev->encryption_key) - 1);
#endif
        }
        dev->use_encryption = true;
        strncpy(dev->encryption_key, encryption_key, sizeof(dev->encryption_key) - 1);
        dev->encryption_key[sizeof(dev->encryption_key) - 1] = '\0';
    } else if (password && strlen(password) > 0) {
        printf("[%s] Deriving key from password: '%s'\n", TAG, password);
        // Derive encryption key from password using PBKDF2
        // ESPHome uses PBKDF2-SHA256 with empty salt, 1000 iterations, 32 bytes
#ifdef ESP_PLATFORM
        // Use ESP-IDF mbedTLS crypto functions
        if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                 (const unsigned char*)password, strlen(password),
                                 (const unsigned char*)"", 0, 1000,
                                 32, (unsigned char*)dev->encryption_key) != 0) {
            printf("[%s] ERROR: Failed to derive encryption key\n", TAG);
            return -1;
        }
#else
        // Use OpenSSL for Linux
        if (PKCS5_PBKDF2_HMAC(password, strlen(password), (const unsigned char*)"", 0, 1000,
                         EVP_sha256(), 32, (unsigned char*)dev->encryption_key) != 1) {
            printf("[%s] ERROR: Failed to derive encryption key\n", TAG);
            return -1;
        }
#endif
        dev->use_encryption = true;
#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "Derived encryption key from password");
#else
        printf("[%s] Derived encryption key from password: ", TAG);
        for (int i = 0; i < 32; i++) {
            printf("%02x", (unsigned char)dev->encryption_key[i]);
        }
        printf("\n");
#endif
    } else {
        printf("[%s] No encryption key or password provided for %s, disabling encryption\n", TAG, hostname);
        dev->use_encryption = false;
    }
    
    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to create socket");
#else
        printf("[%s] Failed to create socket: %s\n", TAG, strerror(errno));
#endif
        return -1;
    }
    
    // Set socket to blocking mode
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        printf("[%s] Failed to get socket flags\n", TAG);
        close(sock);
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        printf("[%s] Failed to set socket to blocking mode\n", TAG);
        close(sock);
        return -1;
    }
    
    // Resolve hostname
    struct hostent *host = gethostbyname(hostname);
    if (!host) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to resolve %s", hostname);
#else
        printf("[%s] Failed to resolve %s: %s\n", TAG, hostname, strerror(errno));
#endif
        close(sock);
        return -1;
    }
    
    // Connect to device
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    memcpy(&dest_addr.sin_addr.s_addr, host->h_addr, host->h_length);
    
    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to connect to %s:%d", hostname, port);
#else
        printf("[%s] Failed to connect to %s:%d: %s\n", TAG, hostname, port, strerror(errno));
#endif
        close(sock);
        return -1;
    }
    
    dev->socket_fd = sock;
    dev->is_connected = true;

    printf("[%s] Connected to %s:%d successfully\n", TAG, hostname, port);

    // First try plain connection to detect encryption requirements
    if (esphome_send_hello(sock) != 0) {
        printf("[%s] ERROR: Failed to send initial Hello\n", TAG);
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        return -1;
    }

    printf("[%s] Sent initial Hello\n", TAG);

    // Check if server requires encryption
    uint8_t response_buffer[256];
    uint32_t response_msg_type;
    int response_len = esphome_read_message(sock, response_buffer, sizeof(response_buffer), &response_msg_type);

    if (response_len == -2) {
        // Server requires encryption
        printf("[%s] Server requires encryption - full Noise implementation pending\n", TAG);
        printf("[%s] Falling back to plain connection (limited functionality)\n", TAG);
        dev->use_encryption = false;  // Disable encryption for now
        
        // Reconnect for plain connection
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        
        // Create new socket for plain connection
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            printf("[%s] Failed to create socket for plain connection\n", TAG);
            return -1;
        }
        
        // Set socket to blocking mode
        int flags = fcntl(sock, F_GETFL, 0);
        if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            printf("[%s] Failed to set socket to blocking mode\n", TAG);
            close(sock);
            return -1;
        }
        
        // Reconnect
        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            printf("[%s] Failed to reconnect for plain connection: %s\n", TAG, strerror(errno));
            close(sock);
            return -1;
        }
        
        dev->socket_fd = sock;
        dev->is_connected = true;
        printf("[%s] Reconnected for plain connection\n", TAG);
        
    } else if (response_len > 0 && response_msg_type == ESPHOME_MSG_HELLO_RESPONSE) {
        // Plain connection successful
        printf("[%s] Plain connection established\n", TAG);
        dev->use_encryption = false;
        
    } else {
        printf("[%s] ERROR: Unexpected response to Hello (len=%d, type=%u)\n", TAG, response_len, response_msg_type);
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        return -1;
    }

    // Send Connect request
    if (esphome_send_connect(sock, password) != 0) {
        printf("[%s] ERROR: Failed to send Connect request\n", TAG);
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        return -1;
    }

    printf("[%s] Sent Connect request\n", TAG);

    // Read Connect response
    uint8_t connect_response[256];
    uint32_t connect_msg_type;
    int connect_len = esphome_read_message(sock, connect_response, sizeof(connect_response), &connect_msg_type);

    if (connect_len <= 0) {
        printf("[%s] ERROR: Failed to read Connect response\n", TAG);
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        return -1;
    }

    if (connect_msg_type != ESPHOME_MSG_CONNECT_RESPONSE) {
        printf("[%s] ERROR: Unexpected connect response type %u\n", TAG, connect_msg_type);
        close(sock);
        dev->socket_fd = -1;
        dev->is_connected = false;
        return -1;
    }

    printf("[%s] Connect response received, connection established\n", TAG);

    return 0;
}

/**
 * @brief Disconnect from ESPHome device
 */
int esphome_api_disconnect(const char *hostname) {
    esphome_device_t *dev = _find_device(hostname);
    if (!dev) {
        return -1;
    }
    
    if (dev->socket_fd >= 0) {
        close(dev->socket_fd);
        dev->socket_fd = -1;
    }
    
    dev->is_connected = false;
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Disconnected from %s", hostname);
#else
    printf("[%s] Disconnected from %s\n", TAG, hostname);
#endif
    
    return 0;
}

/**
 * @brief Get list of connected devices
 */
int esphome_api_get_devices(esphome_device_t *devices, int max_devices, int *count) {
    if (!devices || !count) {
        return -1;
    }
    
    int copy_count = (esphome_count < max_devices) ? esphome_count : max_devices;
    memcpy(devices, esphome_devices, copy_count * sizeof(esphome_device_t));
    *count = copy_count;
    
    return 0;
}

/**
 * @brief Get list of entities from device
 */
int esphome_api_get_entities(const char *hostname, esphome_entity_t *entities, 
                              int max_entities, int *count) {
    if (!hostname || !entities || !count) {
        return -1;
    }
    
    lock_entity_mutex();
    
    *count = 0;
    for (int i = 0; i < g_entity_count && *count < max_entities; i++) {
        if (strcmp(g_entities[i].device_hostname, hostname) == 0) {
            entities[*count] = g_entities[i];
            (*count)++;
        }
    }
    
    unlock_entity_mutex();
    
    return 0;
}

/**
 * @brief Map sensor to virtual zone
 */
int esphome_api_map_sensor_to_zone(const char *hostname, uint32_t entity_key, int zone_id) {
    if (zone_id < 33 || zone_id > 96) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Invalid zone ID: %d", zone_id);
#else
        printf("[%s] ERROR: Invalid zone ID: %d\n", TAG, zone_id);
#endif
        return -1;
    }
    
    lock_entity_mutex();
    
    // Find entity
    esphome_entity_t *entity = NULL;
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == entity_key &&
            strcmp(g_entities[i].device_hostname, hostname) == 0) {
            entity = &g_entities[i];
            break;
        }
    }
    
    if (!entity) {
        unlock_entity_mutex();
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Entity not found");
#else
        printf("[%s] ERROR: Entity not found\n", TAG);
#endif
        return -1;
    }
    
    entity->virtual_zone_id = zone_id;
    
    unlock_entity_mutex();
    entity->virtual_zone_id = zone_id;
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Mapped %s to zone %d", entity->name, zone_id);
#else
    printf("[%s] Mapped %s to zone %d\n", TAG, entity->name, zone_id);
#endif
    
    return 0;
}

/**
 * @brief Map switch to virtual relay
 */
int esphome_api_map_switch_to_relay(const char *hostname, uint32_t entity_key, int relay_id) {
    if (relay_id < 8 || relay_id > 31) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Invalid relay ID: %d", relay_id);
#else
        printf("[%s] ERROR: Invalid relay ID: %d\n", TAG, relay_id);
#endif
        return -1;
    }
    
    lock_entity_mutex();
    
    // Find entity
    esphome_entity_t *entity = NULL;
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == entity_key &&
            strcmp(g_entities[i].device_hostname, hostname) == 0) {
            entity = &g_entities[i];
            break;
        }
    }
    
    if (!entity) {
        unlock_entity_mutex();
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Entity not found");
#else
        printf("[%s] ERROR: Entity not found\n", TAG);
#endif
        return -1;
    }
    
    entity->virtual_relay_id = relay_id;
    
    unlock_entity_mutex();
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Mapped %s to relay %d", entity->name, relay_id);
#else
    printf("[%s] Mapped %s to relay %d\n", TAG, entity->name, relay_id);
#endif
    
    return 0;
}

/**
 * @brief Find entity by virtual relay ID
 */
int esphome_api_find_entity_by_relay(int relay_id, char *hostname_out, size_t hostname_len, uint32_t *entity_key_out) {
    lock_entity_mutex();
    
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].virtual_relay_id == relay_id) {
            strncpy(hostname_out, g_entities[i].device_hostname, hostname_len - 1);
            hostname_out[hostname_len - 1] = '\0';
            *entity_key_out = g_entities[i].entity_key;
            unlock_entity_mutex();
            return 0;
        }
    }
    
    unlock_entity_mutex();
    return -1;
}

/**
 * @brief Control switch/light
 */
int esphome_api_set_switch(const char *hostname, uint32_t entity_key, bool state) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Setting switch %s on %s to %s", 
             entity_key, hostname, state ? "ON" : "OFF");
#else
    printf("[%s] Setting switch %" PRIu32 " on %s to %s\n", 
           TAG, entity_key, hostname, state ? "ON" : "OFF");
#endif
    
    // Find device
    esphome_device_t *dev = _find_device(hostname);
    if (!dev) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Device %s not found", hostname);
#else
        printf("[%s] ERROR: Device %s not found\n", TAG, hostname);
#endif
        return -1;
    }
    
    printf("[%s] Found device %s, entity_key=%" PRIu32 ", encryption_key='%s'\n", 
           TAG, hostname, dev->entity_key, dev->encryption_key);
    
    // For Linux testing, use proper connection with encryption support
#ifndef ESP_PLATFORM
    // Force reconnection to ensure correct encryption settings
    dev->is_connected = false;
    if (esphome_api_connect(hostname, dev->port, dev->password, dev->encryption_key) != 0) {
        printf("[%s] ERROR: Failed to connect to %s\n", TAG, hostname);
        return -1;
    }
    
    // Send switch command (encrypted or plain based on connection type)
    if (dev->use_encryption && dev->handshake.state == NOISE_STATE_READY) {
        // Send encrypted switch command
        uint8_t switch_data[32];
        int switch_data_len = 0;
        
        // Key (field 1, fixed32)
        switch_data[switch_data_len++] = 0x0D;  // Field 1, wire type 5 (fixed32)
        memcpy(&switch_data[switch_data_len], &entity_key, sizeof(entity_key));
        switch_data_len += sizeof(entity_key);
        
        // State (field 2, bool)
        switch_data[switch_data_len++] = 0x10;  // Field 2, wire type 0 (varint)
        switch_data[switch_data_len++] = state ? 0x01 : 0x00;
        
        if (esphome_send_encrypted_message(dev->socket_fd, &dev->handshake, ESPHOME_MSG_SWITCH_COMMAND_REQUEST,
                                          switch_data, switch_data_len) != 0) {
            printf("[%s] ERROR: Failed to send encrypted switch command\n", TAG);
            return -1;
        }
    } else {
        // Send plain switch command
        if (esphome_send_switch_command(dev->socket_fd, entity_key, state) != 0) {
            printf("[%s] ERROR: Failed to send switch command\n", TAG);
            return -1;
        }
    }
    
    printf("[%s] Switch command sent successfully\n", TAG);
    
    // Try to read response (for debugging)
#ifndef ESP_PLATFORM
    uint8_t response_buffer[256];
    uint32_t response_type;
    int response_len;
    
    if (dev->use_encryption && dev->handshake.state == NOISE_STATE_READY) {
        response_len = esphome_read_encrypted_message(dev->socket_fd, &dev->handshake, response_buffer,
                                                     sizeof(response_buffer), &response_type);
    } else {
        response_len = esphome_read_message(dev->socket_fd, response_buffer, sizeof(response_buffer), &response_type);
    }
    
    if (response_len > 0) {
        printf("[%s] Received response type %u, length %d\n", TAG, response_type, response_len);
    } else {
        printf("[%s] No response received from device\n", TAG);
    }
#endif
    
    return 0;
    
#else
    // Original ESP32 implementation
    // Connect if not connected
    if (!dev->is_connected) {
#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "Connecting to %s for switch control", hostname);
#else
        printf("[%s] Connecting to %s for switch control\n", TAG, hostname);
#endif
        if (esphome_api_connect(hostname, dev->port, dev->password, dev->encryption_key) != 0) {
#ifdef ESP_PLATFORM
            ESP_LOGE(TAG, "Failed to connect to %s", hostname);
#else
            printf("[%s] ERROR: Failed to connect to %s\n", TAG, hostname);
#endif
            return -1;
        }
    }
    
    // Send switch command
    if (esphome_send_switch_command(dev->socket_fd, entity_key, state) != 0) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to send switch command");
#else
        printf("[%s] ERROR: Failed to send switch command\n", TAG);
#endif
        return -1;
    }
    
    return 0;
#endif
}

/**
 * @brief Set brightness
 */
int esphome_api_set_brightness(const char *hostname, uint32_t entity_key, float brightness) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Setting brightness on %s to %.2f", hostname, brightness);
#else
    printf("[%s] Setting brightness on %s to %.2f\n", TAG, hostname, brightness);
#endif
    
    // TODO: Send LightCommandRequest message with brightness
    
    return 0;
}

/**
 * @brief ESPHome API client task
 */
#ifdef ESP_PLATFORM
static void esphome_api_task(void *pvParameters) {
    ESP_LOGI(TAG, "ESPHome API task started");
    
    while (1) {
        // Process messages from all connected devices
        // TODO: Implement message receive/dispatch loop
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Should never reach here
    vTaskDelete(NULL);
}
#else
static void *esphome_api_task(void *pvParameters) {
    printf("[%s] ESPHome API task started\n", TAG);
    
    while (1) {
        // Process messages from all connected devices
        // TODO: Implement message receive/dispatch loop
        
        usleep(100000); // 100ms delay
    }
    
    // Should never reach here
    return NULL;
}
#endif

/**
 * @brief Start ESPHome API client task
 */
#ifdef ESP_PLATFORM
__attribute__((unused))
int esphome_api_start_task(void) {
    xTaskCreate(esphome_api_task, "esphome_api", 8192, NULL, 5, NULL);
    return 0;
}
#endif

#ifndef ESP_PLATFORM
// Noise protocol implementation removed - now using noise-c library in esphome_api_protocol.c
#endif

/**
 * @brief Discover entities from all connected ESPHome devices
 */
int esphome_api_discover_all_entities(void) {
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Discovering entities from all ESPHome devices...");
#else
    printf("[%s] Discovering entities from all ESPHome devices...\n", TAG);
#endif
    
    lock_entity_mutex();
    
    // Clear existing entities
    memset(g_entities, 0, sizeof(g_entities));
    g_entity_count = 0;
    
    // For each enabled device, connect and discover entities
    for (int i = 0; i < esphome_count; i++) {
        if (!esphome_devices[i].enabled) {
            continue;
        }
        
        esphome_device_t *dev = &esphome_devices[i];
        
#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "Discovering entities from %s...", dev->hostname);
#else
        printf("[%s] Discovering entities from %s...\n", TAG, dev->hostname);
#endif
        
        // Connect to device if not connected
        if (!dev->is_connected) {
            if (esphome_api_connect(dev->hostname, dev->port, dev->password, dev->encryption_key) != 0) {
#ifdef ESP_PLATFORM
                ESP_LOGE(TAG, "Failed to connect to %s for entity discovery", dev->hostname);
#else
                printf("[%s] ERROR: Failed to connect to %s for entity discovery\n", TAG, dev->hostname);
#endif
                continue;
            }
        }
        
        // Reset counters for this device
        int switch_count = 0;
        int sensor_count = 0;
        
        // Get entities by parsing LIST_ENTITIES responses directly
        // (Can't use esphome_api_get_entities() because registry is empty)
        printf("[%s] Parsing LIST_ENTITIES responses from %s...\n", TAG, dev->hostname);
        
        // Read entity responses
        uint8_t response_buffer[2048];
        uint32_t response_type;
        int response_len;
        int entities_found = 0;
        
        // Read until we get LIST_ENTITIES_DONE
        while ((response_len = esphome_read_message(dev->socket_fd, response_buffer, sizeof(response_buffer), &response_type)) > 0) {
            if (response_type == ESPHOME_MSG_LIST_ENTITIES_SWITCH) {
                // Parse switch entity
                esphome_entity_t entity;
                memset(&entity, 0, sizeof(entity));
                strcpy(entity.device_hostname, dev->hostname);
                entity.type = ESPHOME_ENTITY_SWITCH;
                
                // Simple protobuf parsing
                int pos = 0;
                while (pos < response_len) {
                    uint32_t field_num, wire_type;
                    pos += _decode_varint(&response_buffer[pos], &field_num);
                    wire_type = field_num & 0x07;
                    field_num >>= 3;
                    
                    if (field_num == 1) { // object_id
                        uint32_t len;
                        pos += _decode_varint(&response_buffer[pos], &len);
                        if (len < sizeof(entity.object_id)) {
                            memcpy(entity.object_id, &response_buffer[pos], len);
                            entity.object_id[len] = '\0';
                        }
                        pos += len;
                    } else if (field_num == 2) { // name
                        uint32_t len;
                        pos += _decode_varint(&response_buffer[pos], &len);
                        if (len < sizeof(entity.name)) {
                            memcpy(entity.name, &response_buffer[pos], len);
                            entity.name[len] = '\0';
                        }
                        pos += len;
                    } else if (field_num == 5) { // key
                        pos += _decode_varint(&response_buffer[pos], &entity.entity_key);
                    } else {
                        // Skip unknown fields
                        if (wire_type == 0) {
                            uint32_t dummy;
                            pos += _decode_varint(&response_buffer[pos], &dummy);
                        } else if (wire_type == 2) {
                            uint32_t len;
                            pos += _decode_varint(&response_buffer[pos], &len);
                            pos += len;
                        } else {
                            break;
                        }
                    }
                }
                
                // Add to global registry
                if (g_entity_count < MAX_ESPHOME_DEVICES * 10) {
                    esphome_entity_t *reg_entity = &g_entities[g_entity_count++];
                    memcpy(reg_entity, &entity, sizeof(esphome_entity_t));
                    
                    printf("[%s] DISCOVERED SWITCH: %s (key=%" PRIu32 ") on %s\n", 
                          TAG, reg_entity->name, reg_entity->entity_key, reg_entity->device_hostname);
                    
                    // Set virtual mappings
                    reg_entity->virtual_zone_id = -1;
                    reg_entity->virtual_relay_id = -1;
                    
                    // Map switches to relays
                    if (dev->virtual_relay_start > 0 && reg_entity->type == ESPHOME_ENTITY_SWITCH) {
                        reg_entity->virtual_relay_id = dev->virtual_relay_start + switch_count;
                        switch_count++;
                        
                        printf("[%s] Mapped switch %s to relay %d\n", 
                              TAG, reg_entity->name, reg_entity->virtual_relay_id);
                    }
                    
                    entities_found++;
                }
                
            } else if (response_type == ESPHOME_MSG_LIST_ENTITIES_BINARY_SENSOR) {
                // Parse binary sensor entity
                esphome_entity_t entity;
                memset(&entity, 0, sizeof(entity));
                strcpy(entity.device_hostname, dev->hostname);
                entity.type = ESPHOME_ENTITY_BINARY_SENSOR;
                
                // Similar parsing as switch
                int pos = 0;
                while (pos < response_len) {
                    uint32_t field_num, wire_type;
                    pos += _decode_varint(&response_buffer[pos], &field_num);
                    wire_type = field_num & 0x07;
                    field_num >>= 3;
                    
                    if (field_num == 1) { // object_id
                        uint32_t len;
                        pos += _decode_varint(&response_buffer[pos], &len);
                        if (len < sizeof(entity.object_id)) {
                            memcpy(entity.object_id, &response_buffer[pos], len);
                            entity.object_id[len] = '\0';
                        }
                        pos += len;
                    } else if (field_num == 2) { // name
                        uint32_t len;
                        pos += _decode_varint(&response_buffer[pos], &len);
                        if (len < sizeof(entity.name)) {
                            memcpy(entity.name, &response_buffer[pos], len);
                            entity.name[len] = '\0';
                        }
                        pos += len;
                    } else if (field_num == 5) { // key
                        pos += _decode_varint(&response_buffer[pos], &entity.entity_key);
                    } else {
                        // Skip unknown fields
                        if (wire_type == 0) {
                            uint32_t dummy;
                            pos += _decode_varint(&response_buffer[pos], &dummy);
                        } else if (wire_type == 2) {
                            uint32_t len;
                            pos += _decode_varint(&response_buffer[pos], &len);
                            pos += len;
                        } else {
                            break;
                        }
                    }
                }
                
                // Add to global registry
                if (g_entity_count < MAX_ESPHOME_DEVICES * 10) {
                    esphome_entity_t *reg_entity = &g_entities[g_entity_count++];
                    memcpy(reg_entity, &entity, sizeof(esphome_entity_t));
                    
                    printf("[%s] DISCOVERED BINARY SENSOR: %s (key=%" PRIu32 ") on %s\n", 
                          TAG, reg_entity->name, reg_entity->entity_key, reg_entity->device_hostname);
                    
                    // Set virtual mappings
                    reg_entity->virtual_zone_id = -1;
                    reg_entity->virtual_relay_id = -1;
                    
                    // Map sensors to zones
                    if (dev->virtual_zone_start > 0 && reg_entity->type == ESPHOME_ENTITY_BINARY_SENSOR) {
                        reg_entity->virtual_zone_id = dev->virtual_zone_start + sensor_count;
                        sensor_count++;
                        
                        printf("[%s] Mapped sensor %s to zone %d\n", 
                              TAG, reg_entity->name, reg_entity->virtual_zone_id);
                    }
                    
                    entities_found++;
                }
                
            } else if (response_type == ESPHOME_MSG_LIST_ENTITIES_DONE) {
                printf("[%s] Entity discovery complete for %s, found %d entities\n", 
                      TAG, dev->hostname, entities_found);
                break;
            }
            
            usleep(10000); // Small delay between reads
        }
    }
    
    unlock_entity_mutex();
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Entity discovery complete. Found %d total entities", g_entity_count);
#else
    printf("[%s] Entity discovery complete. Found %d total entities\n", TAG, g_entity_count);
#endif
    
    return 0;
}
