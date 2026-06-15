#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../components/esphome_api/esphome_api_client.h"
#include "../components/storage/storage_mgr.h"

int main() {
    printf("=== ESPHome Entity Discovery Test ===\n");

    // Initialize storage
    if (storage_init() != 0) {
        printf("ERROR: Failed to initialize storage\n");
        return 1;
    }

    // Initialize ESPHome API
    if (esphome_api_init() != 0) {
        printf("ERROR: Failed to initialize ESPHome API\n");
        return 1;
    }

    printf("ESPHome API initialized. Checking discovered entities...\n");

    // Get all entities
    esphome_entity_t entities[32];
    int count = 0;

    if (esphome_api_get_entities(NULL, entities, 32, &count) == 0) {
        printf("Found %d entities:\n", count);
        for (int i = 0; i < count; i++) {
            printf("  Entity %d:\n", i+1);
            printf("    Hostname: %s\n", entities[i].device_hostname);
            printf("    Name: %s\n", entities[i].name);
            printf("    Object ID: %s\n", entities[i].object_id);
            printf("    Entity Key: %" PRIu32 "\n", entities[i].entity_key);
            printf("    Type: %d (%s)\n", entities[i].type,
                   entities[i].type == ESPHOME_ENTITY_SWITCH ? "Switch" :
                   entities[i].type == ESPHOME_ENTITY_BINARY_SENSOR ? "Binary Sensor" : "Other");
            printf("    Virtual Relay: %d\n", entities[i].virtual_relay_id);
            printf("    Virtual Zone: %d\n", entities[i].virtual_zone_id);
            printf("    Current State: %s\n", entities[i].current_state ? "ON" : "OFF");
            printf("\n");
        }
    } else {
        printf("ERROR: Failed to get entities\n");
    }

    return 0;
}