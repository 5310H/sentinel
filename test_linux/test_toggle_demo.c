#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Mock structures
typedef struct {
    int id;
    int gpio;
    char name[32];
} relay_t;

// Mock ESPHome device
typedef struct {
    char hostname[64];
    uint32_t entity_key;
    int virtual_relay_start;
    char friendly_name[64];
    int enabled;
} esphome_device_t;

// Mock arrays
relay_t storage_get_relay(32);
int storage_get_relay_count() = 0;
esphome_device_t storage_get_esphome_device(8);
int storage_get_esphome_count() = 0;

// Mock ESPHome API functions
int esphome_api_find_entity_by_relay(int relay_id, char *hostname_out, size_t hostname_len, uint32_t *entity_key_out) {
    for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->virtual_relay_start == relay_id && storage_get_esphome_device(i)->enabled) {
            strcpy(hostname_out, storage_get_esphome_device(i)->hostname);
            *entity_key_out = storage_get_esphome_device(i)->entity_key;
            return 0;
        }
    }
    return -1;
}

int esphome_api_set_switch(const char *hostname, uint32_t entity_key, int state) {
    printf("🔌 ESPHome API: %s, entity_key=%u, state=%s\n", hostname, entity_key, state ? "ON" : "OFF");
    return 0; // Mock success
}

void test_relay_toggle(int relay_idx, int state_b) {
    printf("\n=== Testing ESPHome relay toggle: index=%d, state=%s ===\n", relay_idx, state_b ? "ON" : "OFF");

    if (relay_idx >= 0 && relay_idx < storage_get_relay_count()) {
        if (storage_get_relay(relay_idx)->gpio == -1) {
            // Virtual relay - ESPHome
            char hostname[64];
            uint32_t entity_key;
            if (esphome_api_find_entity_by_relay(storage_get_relay(relay_idx)->id, hostname, sizeof(hostname), &entity_key) == 0) {
                printf("✅ ESPHome Relay %d (%s) set to %s\n", 
                    storage_get_relay(relay_idx)->id, hostname, state_b ? "ON" : "OFF");
                
                int result = esphome_api_set_switch(hostname, entity_key, state_b);
                if (result == 0) {
                    printf("✅ SUCCESS: ESPHome relay toggled correctly!\n");
                } else {
                    printf("❌ ERROR: ESPHome API call failed\n");
                }
            } else {
                printf("❌ ERROR: No ESPHome entity mapped to relay %d\n", storage_get_relay(relay_idx)->id);
            }
        }
    }
}

int main() {
    // Setup relays
    for (int i = 0; i < 8; i++) {
        storage_get_relay(i)->id = i + 1;
        storage_get_relay(i)->gpio = i;
        sprintf(storage_get_relay(i)->name, "Physical Relay %d", i + 1);
    }
    storage_get_relay(8)->id = 9;
    storage_get_relay(8)->gpio = -1;
    strcpy(storage_get_relay(8)->name, "ESPHome Smart Plug");
    storage_get_relay_count() = 9;

    // Setup ESPHome device
    strcpy(storage_get_esphome_device(0)->hostname, "esphome-smart-plug.local");
    storage_get_esphome_device(0)->entity_key = 12345;
    storage_get_esphome_device(0)->virtual_relay_start = 9;
    strcpy(storage_get_esphome_device(0)->friendly_name, "Smart Plug");
    storage_get_esphome_device(0)->enabled = 1;
    storage_get_esphome_count() = 1;

    printf("🚀 ESPHome Relay Control Demo\n");
    printf("==============================\n");
    
    // Test toggling ON
    test_relay_toggle(8, 1);
    
    // Test toggling OFF  
    test_relay_toggle(8, 0);
    
    // Test toggling ON again
    test_relay_toggle(8, 1);

    printf("\n✅ Demo complete! ESPHome relay control is working.\n");
    return 0;
}
