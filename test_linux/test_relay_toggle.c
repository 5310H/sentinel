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
relay_t relays[32];
int r_count = 0;
esphome_device_t esphome_devices[8];
int esphome_count = 0;

// Mock ESPHome API functions
int esphome_api_find_entity_by_relay(int relay_id, char *hostname_out, size_t hostname_len, uint32_t *entity_key_out) {
    for (int i = 0; i < esphome_count; i++) {
        if (esphome_devices[i].virtual_relay_start == relay_id && esphome_devices[i].enabled) {
            strcpy(hostname_out, esphome_devices[i].hostname);
            *entity_key_out = esphome_devices[i].entity_key;
            return 0;
        }
    }
    return -1;
}

int esphome_api_set_switch(const char *hostname, uint32_t entity_key, int state) {
    printf("[MOCK] ESPHome API call: %s, entity_key=%u, state=%d\n", hostname, entity_key, state);
    return 0; // Mock success
}
    return 0;
}

// Test the relay toggle logic
void test_relay_toggle(int relay_idx, int state_b) {
    printf("\n=== Testing relay toggle: index=%d, state=%s ===\n", relay_idx, state_b ? "ON" : "OFF");

    // Find the relay by array index (our fixed logic)
    int found = 0;

    // Check if index is valid
    if (relay_idx >= 0 && relay_idx < r_count) {
        if (relays[relay_idx].gpio == -1) {
            // Virtual relay - ESPHome
            printf("[DEBUG] Virtual relay at index %d, id=%d\n", relay_idx, relays[relay_idx].id);
            
            // Find the ESPHome entity mapped to this relay
            char hostname[64];
            uint32_t entity_key;
            if (esphome_api_find_entity_by_relay(relays[relay_idx].id, hostname, sizeof(hostname), &entity_key) == 0) {
                printf("[ESPHome] Relay %d found on device %s, entity_key=%" PRIu32 "\n", 
                    relays[relay_idx].id, hostname, entity_key);
                
                // Call ESPHome API
                printf("[DEBUG] About to call esphome_api_set_switch for relay %d\n", relays[relay_idx].id);
                int result = esphome_api_set_switch(hostname, entity_key, state_b);
                printf("[DEBUG] esphome_api_set_switch returned: %d\n", result);
                
                if (result == 0) {
                    found = 1;
                }
            } else {
                printf("[DEBUG] No ESPHome entity mapped to relay %d\n", relays[relay_idx].id);
            }

                    if (result == 0) {
                        printf("[SUCCESS] ESPHome relay toggled correctly!\n");
                    }
                    found = 1;
                    break;
                }
            }
        } else {
            // Physical relay
            printf("[Physical] Relay %d (%s) set to %s\n", relays[relay_idx].id, relays[relay_idx].name, state_b ? "ON" : "OFF");
            found = 1;
        }
    }

    if (!found) {
        printf("[ERROR] Relay not found!\n");
    }
}

int main() {
    // Setup test data
    // Physical relays 0-7 (IDs 1-8)
    for (int i = 0; i < 8; i++) {
        relays[i].id = i + 1;
        relays[i].gpio = i;
        sprintf(relays[i].name, "Physical Relay %d", i + 1);
    }
    r_count = 8;

    // ESPHome relay at index 8 (ID 9)
    relays[8].id = 9;
    relays[8].gpio = -1;
    strcpy(relays[8].name, "ESPHome Smart Plug");
    r_count = 9;

    // ESPHome device
    strcpy(esphome_devices[0].hostname, "esphome-smart-plug.local");
    esphome_devices[0].entity_key = 12345;
    esphome_devices[0].virtual_relay_start = 9;
    strcpy(esphome_devices[0].friendly_name, "Smart Plug");
    esphome_devices[0].enabled = 1;
    esphome_count = 1;

    printf("Relay Array:\n");
    for (int i = 0; i < r_count; i++) {
        printf("  [%d] ID=%d, GPIO=%d, Name='%s'\n", i, relays[i].id, relays[i].gpio, relays[i].name);
    }

    // Test cases
    printf("\n--- Test Case 1: Physical relay at index 0 (should be relay ID 1) ---\n");
    test_relay_toggle(0, 1);

    printf("\n--- Test Case 2: Physical relay at index 7 (should be relay ID 8) ---\n");
    test_relay_toggle(7, 1);

    printf("\n--- Test Case 3: ESPHome relay at index 8 (should be relay ID 9) ---\n");
    test_relay_toggle(8, 1);

    printf("\n--- Test Case 4: Invalid index ---\n");
    test_relay_toggle(99, 1);

    return 0;
}