/**
 * @file esphome_mock.c
 * @brief Mock ESPHome devices for Linux testing
 * 
 * Uses actual devices loaded from esphome.json via storage_mgr
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../components/engine/engine.h"
#include "../components/storage/storage_mgr.h"

// Access mock relay states from main_mock.c
extern bool mock_esphome_relay_states[24];

// Use the actual loaded devices from storage_mgr
/**
 * @brief Initialize mock ESPHome devices (uses actual loaded config)
 */
void mock_esphome_init(void) {
    printf("[MOCK] Initializing %d ESPHome devices\n", storage_get_esphome_count());
    
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        printf("[MOCK] Device %d: %s (%s:%d)\n", i + 1, dev->friendly_name, dev->hostname, dev->port);
        printf("       Description: %s\n", dev->description);
        printf("       Location: %s\n", dev->location);
        printf("       Enabled: %s\n", dev->enabled ? "Yes" : "No");
        
        if (dev->virtual_zone_start > 0) {
            printf("       Mapped to Zone %d\n", dev->virtual_zone_start);
        }
        if (dev->virtual_relay_start > 0) {
            printf("       Mapped to Relay %d\n", dev->virtual_relay_start);
        }
    }
}

/**
 * @brief Trigger a sensor state change (for testing) - searches by hostname or friendly name
 */
void mock_esphome_trigger_sensor(const char *entity_id, bool state) {
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        
        // Match by hostname (without .local) or friendly_name
        char hostname_short[STR_SMALL];
        strncpy(hostname_short, dev->hostname, sizeof(hostname_short) - 1);
        char *dot = strstr(hostname_short, ".local");
        if (dot) *dot = '\0';
        
        if (strcmp(hostname_short, entity_id) == 0 || 
            strcmp(dev->friendly_name, entity_id) == 0) {
            
            printf("[MOCK] Triggering %s: state=%s\n",
                   dev->friendly_name, state ? "ON" : "OFF");
            
            // Simulate zone trip if mapped
            if (dev->virtual_zone_start > 0 && dev->enabled && state) {
                printf("[MOCK] Zone %d tripped by %s\n", 
                       dev->virtual_zone_start, dev->friendly_name);
                
                // Call engine zone processing (zones array is 0-indexed)
                engine_process_zone_trip(dev->virtual_zone_start - 1);
            }
            
            return;
        }
    }
    
    printf("[MOCK] Entity '%s' not found\n", entity_id);
}

/**
 * @brief Set a switch state (for testing) - searches by hostname or friendly name
 */
void mock_esphome_set_switch(const char *entity_id, bool state) {
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        
        // Match by hostname (without .local) or friendly_name
        char hostname_short[STR_SMALL];
        strncpy(hostname_short, dev->hostname, sizeof(hostname_short) - 1);
        char *dot = strstr(hostname_short, ".local");
        if (dot) *dot = '\0';
        
        if (strcmp(hostname_short, entity_id) == 0 || 
            strcmp(dev->friendly_name, entity_id) == 0) {
            
            printf("[MOCK] Setting %s: %s\n",
                   dev->friendly_name, state ? "ON" : "OFF");
            
            // Update mock relay state if this device has a virtual relay
            if (dev->virtual_relay_start > 0) {
                int esphome_relay_idx = dev->virtual_relay_start - 8;
                if (esphome_relay_idx >= 0 && esphome_relay_idx < 24) {
                    mock_esphome_relay_states[esphome_relay_idx] = state;
                }
            }
            
            return;
        }
    }
    
    printf("[MOCK] Entity '%s' not found\n", entity_id);
}

/**
 * @brief List all mock devices (uses actual loaded config)
 */
void mock_esphome_list_devices(void) {
    printf("\n=== ESPHome Devices (from esphome.json) ===\n");
    
    if (storage_get_esphome_count() == 0) {
        printf("No ESPHome devices configured.\n\n");
        return;
    }
    
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        
        printf("\nDevice %d:\n", i + 1);
        printf("  Hostname: %s:%d\n", dev->hostname, dev->port);
        printf("  Name: %s\n", dev->friendly_name);
        printf("  Description: %s\n", dev->description);
        printf("  Location: %s\n", dev->location);
        printf("  Enabled: %s\n", dev->enabled ? "Yes" : "No");
        
        if (dev->virtual_zone_start > 0) {
            printf("  Virtual Zone: %d\n", dev->virtual_zone_start);
        }
        if (dev->virtual_relay_start > 0) {
            printf("  Virtual Relay: %d\n", dev->virtual_relay_start);
        }
        if (dev->password[0]) {
            printf("  Password: ***\n");
        }
        if (dev->encryption_key[0]) {
            printf("  Encryption: Enabled\n");
        }
    }
    
    printf("\n");
}

/**
 * @brief Get device status as JSON (uses actual loaded config)
 */
char* mock_esphome_get_json(void) {
    static char json_buffer[4096];
    int offset = 0;
    
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                      "{\"count\":%d,\"devices\":[", storage_get_esphome_count());
    
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        
        if (i > 0) {
            offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, ",");
        }
        
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                          "{\"hostname\":\"%s\","
                          "\"port\":%d,"
                          "\"friendly_name\":\"%s\","
                          "\"description\":\"%s\","
                          "\"location\":\"%s\","
                          "\"enabled\":%s",
                          dev->hostname,
                          dev->port,
                          dev->friendly_name,
                          dev->description,
                          dev->location,
                          dev->enabled ? "true" : "false");
        
        if (dev->virtual_zone_start > 0) {
            offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                             ",\"virtual_zone_start\":%d", dev->virtual_zone_start);
        }
        if (dev->virtual_relay_start > 0) {
            offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                             ",\"virtual_relay_start\":%d", dev->virtual_relay_start);
        }
        
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "}");
    }
    
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "]}");
    
    return json_buffer;
}
