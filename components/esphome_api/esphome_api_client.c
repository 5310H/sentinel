/**
 * @file esphome_api_client.c
 * @brief ESPHome Native API client implementation wrapping esphome-c-api
 */

#include "esphome_api_client.h"
#include "esphome_api.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static const char *TAG = "ESPHOME_API";
(void)TAG;
#else
#include <unistd.h>
#include <pthread.h>
#define TAG "ESPHOME_API"
#endif

#include "../storage/storage_mgr.h"

// Global entity registry
static esphome_entity_t g_entities[MAX_ESPHOME_DEVICES * 10];
static int g_entity_count = 0;

#ifdef ESP_PLATFORM
static SemaphoreHandle_t g_entity_mutex = NULL;
#else
static pthread_mutex_t g_entity_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

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

static esphome_device_t* _find_device(const char *hostname) {
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (strcmp(storage_get_esphome_device(i)->hostname, hostname) == 0) {
            return storage_get_esphome_device(i);
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// External Registry callbacks used by esphome-c-api
// ---------------------------------------------------------------------------
void esph_registry_add(const char *object_id, uint32_t key, uint32_t legacy_type) {
    lock_entity_mutex();
    
    // Check if already exists
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == key) {
            unlock_entity_mutex();
            return;
        }
    }
    
    if (g_entity_count < MAX_ESPHOME_DEVICES * 10) {
        esphome_entity_t *entity = &g_entities[g_entity_count++];
        memset(entity, 0, sizeof(esphome_entity_t));
        entity->entity_key = key;
        strncpy(entity->object_id, object_id, sizeof(entity->object_id) - 1);
        strncpy(entity->name, object_id, sizeof(entity->name) - 1);
        
        // Map legacy proto type to our internal type
        // ListEntitiesSwitchResponse = 14 (from esphome_api.pb.h or similar)
        // ListEntitiesBinarySensorResponse = 12
        if (legacy_type == 14) {
            entity->type = ESPHOME_ENTITY_SWITCH;
        } else if (legacy_type == 12) {
            entity->type = ESPHOME_ENTITY_BINARY_SENSOR;
        } else {
            entity->type = ESPHOME_ENTITY_SENSOR;
        }
        
#ifndef ESP_PLATFORM
        printf("[%s] Discovered Entity: %s (Key: %u, Type: %u)\n", TAG, object_id, key, legacy_type);
#endif
    }
    unlock_entity_mutex();
}

void esph_registry_update_state(uint32_t key, const char *state_str) {
    lock_entity_mutex();
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == key) {
            if (strcmp(state_str, "ON") == 0) {
                g_entities[i].current_state = true;
            } else if (strcmp(state_str, "OFF") == 0) {
                g_entities[i].current_state = false;
            }
#ifndef ESP_PLATFORM
            printf("[%s] Entity %u State Updated: %s\n", TAG, key, state_str);
#endif
            break;
        }
    }
    unlock_entity_mutex();
}

uint32_t esph_registry_lookup_key(const char *entity_id) {
    uint32_t key = 0;
    lock_entity_mutex();
    for (int i = 0; i < g_entity_count; i++) {
        if (strcmp(g_entities[i].object_id, entity_id) == 0 || strcmp(g_entities[i].name, entity_id) == 0) {
            key = g_entities[i].entity_key;
            break;
        }
    }
    unlock_entity_mutex();
    return key;
}

// ---------------------------------------------------------------------------
// Sentinel API Functions
// ---------------------------------------------------------------------------

int esphome_api_init(void) {
#ifdef ESP_PLATFORM
    if (g_entity_mutex == NULL) {
        g_entity_mutex = xSemaphoreCreateMutex();
    }
#endif

    // Reset sessions
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        storage_get_esphome_device(i)->session = NULL;
        storage_get_esphome_device(i)->is_connected = false;
    }
    
    memset(g_entities, 0, sizeof(g_entities));
    g_entity_count = 0;
    
    return 0;
}

int esphome_api_connect(const char *hostname, uint16_t port, const char *password, const char *encryption_key) {
    esphome_device_t *dev = _find_device(hostname);
    if (!dev) return -1;
    
    if (dev->is_connected && dev->session != NULL) {
        return 0; // Already connected
    }

    const char *psk = NULL;
    if (encryption_key && strlen(encryption_key) > 0) {
        psk = encryption_key;
    }
    
#ifndef ESP_PLATFORM
    printf("[%s] Connecting to %s:%d (Encryption: %s)\n", TAG, hostname, port, psk ? "YES" : "NO");
#endif

    esph_session_t *session = esph_connect(hostname, port, psk);
    if (!session) {
#ifndef ESP_PLATFORM
        printf("[%s] Connection failed for %s\n", TAG, hostname);
#endif
        return -1;
    }
    
    dev->session = session;
    dev->is_connected = true;
    dev->use_encryption = (psk != NULL);
    
    // Send Device Info Request
    esph_check_device_info(session);
    
    // Discover entities
    esph_send_list_entities(session);
    esph_wait_list_entities_done(session);
    
    // Subscribe to state updates
    esph_subscribe_states(session);
    
    return 0;
}

int esphome_api_disconnect(const char *hostname) {
    esphome_device_t *dev = _find_device(hostname);
    if (!dev || !dev->session) return -1;
    
    esph_disconnect(dev->session);
    dev->session = NULL;
    dev->is_connected = false;
    return 0;
}

int esphome_api_get_devices(esphome_device_t *devices, int max_devices, int *count) {
    int copy_count = (storage_get_esphome_count() < max_devices) ? storage_get_esphome_count() : max_devices;
    for (int i = 0; i < copy_count; i++) {
        memcpy(&devices[i], storage_get_esphome_device(i), sizeof(esphome_device_t));
    }
    *count = copy_count;
    return 0;
}

int esphome_api_get_entities(const char *hostname, esphome_entity_t *entities, int max_entities, int *count) {
    lock_entity_mutex();
    *count = 0;
    for (int i = 0; i < g_entity_count && *count < max_entities; i++) {
        // Since registry lacks device association directly in the callback,
        // we might just return all or map them appropriately if needed.
        // For now, return all entities discovered on this connection phase.
        entities[*count] = g_entities[i];
        (*count)++;
    }
    unlock_entity_mutex();
    return 0;
}

int esphome_api_map_sensor_to_zone(const char *hostname, uint32_t entity_key, int zone_id) {
    lock_entity_mutex();
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == entity_key) {
            g_entities[i].virtual_zone_id = zone_id;
            break;
        }
    }
    unlock_entity_mutex();
    return 0;
}

int esphome_api_map_switch_to_relay(const char *hostname, uint32_t entity_key, int relay_id) {
    lock_entity_mutex();
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].entity_key == entity_key) {
            g_entities[i].virtual_relay_id = relay_id;
            break;
        }
    }
    unlock_entity_mutex();
    return 0;
}

int esphome_api_find_entity_by_relay(int relay_id, char *hostname_out, size_t hostname_len, uint32_t *entity_key_out) {
    lock_entity_mutex();
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].virtual_relay_id == relay_id) {
            *entity_key_out = g_entities[i].entity_key;
            unlock_entity_mutex();
            return 0;
        }
    }
    unlock_entity_mutex();
    return -1;
}

int esphome_api_set_switch(const char *hostname, uint32_t entity_key, bool state) {
    esphome_device_t *dev = _find_device(hostname);
    if (!dev || !dev->session) return -1;
    
    // We need to look up the string name for esph_set_switch, but wait...
    // esph_set_switch takes the entity name and resolves it to key.
    // Let's modify our use to directly call the internal proto_helpers if needed,
    // or just look up the name from our registry.
    
    char entity_name[128] = {0};
    lock_entity_mutex();
    for(int i = 0; i < g_entity_count; i++) {
        if(g_entities[i].entity_key == entity_key) {
            strcpy(entity_name, g_entities[i].object_id);
            break;
        }
    }
    unlock_entity_mutex();
    
    if(entity_name[0] != '\0') {
        return esph_set_switch(dev->session, entity_name, state ? 1 : 0);
    }
    return -1;
}

int esphome_api_set_brightness(const char *hostname, uint32_t entity_key, float brightness) {
    return 0; // Not implemented in base esphome-c-api yet
}

int esphome_api_discover_all_entities(void) {
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->enabled && storage_get_esphome_device(i)->is_connected) {
            esph_send_list_entities(storage_get_esphome_device(i)->session);
            esph_wait_list_entities_done(storage_get_esphome_device(i)->session);
        }
    }
    return 0;
}

#ifdef ESP_PLATFORM
static void esphome_api_task(void *pvParameters) {
    while (1) {
        for (int i = 0; i < storage_get_esphome_count(); i++) {
            if (storage_get_esphome_device(i)->enabled && storage_get_esphome_device(i)->is_connected && storage_get_esphome_device(i)->session) {
                esph_run_step(storage_get_esphome_device(i)->session, 10);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
int esphome_api_start_task(void) {
    xTaskCreate(esphome_api_task, "esphome_api", 8192, NULL, 5, NULL);
    return 0;
}
#else
static void *esphome_api_task(void *pvParameters) {
    while (1) {
        for (int i = 0; i < storage_get_esphome_count(); i++) {
            if (storage_get_esphome_device(i)->enabled && storage_get_esphome_device(i)->is_connected && storage_get_esphome_device(i)->session) {
                esph_run_step(storage_get_esphome_device(i)->session, 10);
            }
        }
        usleep(50000);
    }
    return NULL;
}
#endif
