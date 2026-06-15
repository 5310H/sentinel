/**
 * @file matter_mock.c
 * @brief Mock implementation of Matter controller for Linux testing
 * 
 * Provides simulated Matter devices without actual Matter stack.
 * Allows testing Matter integration logic without ESP32 hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

// Mock Matter device structure (simplified)
typedef enum {
    MATTER_DEVICE_UNKNOWN = 0,
    MATTER_DEVICE_ON_OFF_SWITCH = 1,
    MATTER_DEVICE_DIMMABLE_LIGHT = 2,
    MATTER_DEVICE_COLOR_LIGHT = 3,
    MATTER_DEVICE_OCCUPANCY_SENSOR = 4,
    MATTER_DEVICE_CONTACT_SENSOR = 5,
    MATTER_DEVICE_DOOR_LOCK = 6,
} matter_device_type_t;

typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    matter_device_type_t device_type;
    char name[64];
    bool is_commissioned;
    bool is_online;
    int8_t virtual_zone_id;
    int8_t virtual_relay_id;
    bool current_state;
    uint8_t brightness;
} matter_device_t;

#define MAX_MOCK_DEVICES 16
static matter_device_t mock_devices[MAX_MOCK_DEVICES];
static int mock_device_count = 0;

// Simulated Matter devices (pre-configured for testing)
static void _init_mock_devices(void) {
    // Device 1: Garage Motion Sensor
    mock_devices[0] = (matter_device_t){
        .node_id = 0x1234567890ABCDEF,
        .endpoint_id = 1,
        .device_type = MATTER_DEVICE_OCCUPANCY_SENSOR,
        .is_commissioned = true,
        .is_online = true,
        .virtual_zone_id = 33,
        .virtual_relay_id = -1,
        .current_state = false,
    };
    snprintf(mock_devices[0].name, sizeof(mock_devices[0].name), "Garage Motion");
    
    // Device 2: Front Door Light
    mock_devices[1] = (matter_device_t){
        .node_id = 0xFEDCBA0987654321,
        .endpoint_id = 1,
        .device_type = MATTER_DEVICE_DIMMABLE_LIGHT,
        .is_commissioned = true,
        .is_online = true,
        .virtual_zone_id = -1,
        .virtual_relay_id = 8,
        .current_state = false,
        .brightness = 0,
    };
    snprintf(mock_devices[1].name, sizeof(mock_devices[1].name), "Front Door Light");
    
    // Device 3: Back Door Contact Sensor
    mock_devices[2] = (matter_device_t){
        .node_id = 0xAABBCCDD11223344,
        .endpoint_id = 1,
        .device_type = MATTER_DEVICE_CONTACT_SENSOR,
        .is_commissioned = true,
        .is_online = true,
        .virtual_zone_id = 34,
        .virtual_relay_id = -1,
        .current_state = false,
    };
    snprintf(mock_devices[2].name, sizeof(mock_devices[2].name), "Back Door Contact");
    
    mock_device_count = 3;
    
    printf("[MATTER_MOCK] Initialized %d simulated devices\n", mock_device_count);
}

// Mock API functions

int matter_controller_init(void) {
    printf("[MATTER_MOCK] Controller initialized\n");
    _init_mock_devices();
    return 0;
}

int matter_zones_init(void) {
    printf("[MATTER_MOCK] Virtual zones initialized (33-96)\n");
    return 0;
}

int matter_relays_init(void) {
    printf("[MATTER_MOCK] Virtual relays initialized (8-31)\n");
    return 0;
}

int matter_controller_start_task(void) {
    printf("[MATTER_MOCK] Event task started (mock)\n");
    return 0;
}

int matter_commission_device(const char *pairing_code, const char *device_name) {
    if (mock_device_count >= MAX_MOCK_DEVICES) {
        printf("[MATTER_MOCK] Device registry full\n");
        return -1;
    }
    
    // Simulate commissioning
    uint64_t node_id = 0x1000000000000000ULL + mock_device_count;
    
    mock_devices[mock_device_count] = (matter_device_t){
        .node_id = node_id,
        .endpoint_id = 1,
        .device_type = MATTER_DEVICE_ON_OFF_SWITCH,
        .is_commissioned = true,
        .is_online = true,
        .virtual_zone_id = -1,
        .virtual_relay_id = -1,
        .current_state = false,
    };
    
    snprintf(mock_devices[mock_device_count].name, 
             sizeof(mock_devices[mock_device_count].name), 
             "%s", device_name ? device_name : "New Device");
    
    mock_device_count++;
    
    printf("[MATTER_MOCK] Commissioned device: %s (Node 0x%" PRIx64 ")\n", device_name, node_id);
    
    return 0;
}

int matter_remove_device(uint64_t node_id) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            // Shift array
            for (int j = i; j < mock_device_count - 1; j++) {
                mock_devices[j] = mock_devices[j + 1];
            }
            mock_device_count--;
            printf("[MATTER_MOCK] Removed device 0x%" PRIx64 "\n", node_id);
            return 0;
        }
    }
    return -1;
}

int matter_get_devices(matter_device_t *devices, int max_devices, int *count) {
    if (!devices || !count) return -1;
    
    int copy_count = (mock_device_count < max_devices) ? mock_device_count : max_devices;
    memcpy(devices, mock_devices, copy_count * sizeof(matter_device_t));
    *count = copy_count;
    
    return 0;
}

int matter_map_sensor_to_zone(uint64_t node_id, uint16_t endpoint_id, int zone_id) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            mock_devices[i].virtual_zone_id = zone_id;
            printf("[MATTER_MOCK] Mapped %s to zone %d\n", mock_devices[i].name, zone_id);
            return 0;
        }
    }
    return -1;
}

int matter_map_switch_to_relay(uint64_t node_id, uint16_t endpoint_id, int relay_id) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            mock_devices[i].virtual_relay_id = relay_id;
            printf("[MATTER_MOCK] Mapped %s to relay %d\n", mock_devices[i].name, relay_id);
            return 0;
        }
    }
    return -1;
}

int matter_send_on_off(uint64_t node_id, uint16_t endpoint_id, bool on) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            mock_devices[i].current_state = on;
            printf("[MATTER_MOCK] Device %s: %s\n", mock_devices[i].name, on ? "ON" : "OFF");
            return 0;
        }
    }
    return -1;
}

int matter_send_brightness(uint64_t node_id, uint16_t endpoint_id, uint8_t level) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            mock_devices[i].brightness = level;
            printf("[MATTER_MOCK] Device %s: Brightness %d\n", mock_devices[i].name, level);
            return 0;
        }
    }
    return -1;
}

int matter_send_lock_unlock(uint64_t node_id, uint16_t endpoint_id, bool lock, const char *pin_code) {
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].node_id == node_id) {
            mock_devices[i].current_state = lock;
            printf("[MATTER_MOCK] Lock %s: %s\n", mock_devices[i].name, lock ? "LOCKED" : "UNLOCKED");
            return 0;
        }
    }
    return -1;
}

int matter_save_config(void) {
    printf("[MATTER_MOCK] Configuration saved\n");
    return 0;
}

int matter_load_config(void) {
    printf("[MATTER_MOCK] Configuration loaded\n");
    return 0;
}

int matter_trigger_alarm_response(void) {
    printf("[MATTER_MOCK] ⚠️  ALARM RESPONSE: Activating all lights!\n");
    
    int activated = 0;
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].device_type == MATTER_DEVICE_DIMMABLE_LIGHT ||
            mock_devices[i].device_type == MATTER_DEVICE_COLOR_LIGHT) {
            mock_devices[i].current_state = true;
            mock_devices[i].brightness = 254;
            printf("[MATTER_MOCK]   → %s: ON (100%%)\n", mock_devices[i].name);
            activated++;
        }
    }
    
    return activated;
}

int matter_clear_alarm_response(void) {
    printf("[MATTER_MOCK] Clearing alarm response\n");
    
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].current_state) {
            mock_devices[i].current_state = false;
            printf("[MATTER_MOCK]   → %s: OFF\n", mock_devices[i].name);
        }
    }
    
    return 0;
}

// Test helper: Simulate sensor trigger
void mock_matter_trigger_sensor(int zone_id) {
    printf("\n[MATTER_MOCK] 🚨 Simulating sensor trigger: Zone %d\n", zone_id);
    
    // Find sensor mapped to this zone
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].virtual_zone_id == zone_id) {
            printf("[MATTER_MOCK] Sensor '%s' triggered\n", mock_devices[i].name);
            mock_devices[i].current_state = true;
            
            // Trigger engine via external callback
            extern int engine_get_arm_state(void);
            extern void engine_process_zone_trip(int zone_id);
            
            if (engine_get_arm_state() >= 2) {
                printf("[MATTER_MOCK] System armed - triggering alarm for zone %d\n", zone_id);
                engine_process_zone_trip(zone_id);
            } else {
                printf("[MATTER_MOCK] System disarmed - no alarm triggered\n");
            }
            
            return;
        }
    }
    
    printf("[MATTER_MOCK] No sensor mapped to zone %d\n", zone_id);
}

// Test helper: List all devices
void mock_matter_list_devices(void) {
    printf("\n[MATTER_MOCK] === Matter Devices (%d) ===\n", mock_device_count);
    for (int i = 0; i < mock_device_count; i++) {
        printf("  [%d] %s\n", i, mock_devices[i].name);
        printf("      Node ID: 0x%" PRIx64 "\n", mock_devices[i].node_id);
        printf("      Type: %d, Online: %s\n", 
               mock_devices[i].device_type,
               mock_devices[i].is_online ? "Yes" : "No");
        if (mock_devices[i].virtual_zone_id >= 0) {
            printf("      Mapped to Zone: %d\n", mock_devices[i].virtual_zone_id);
        }
        if (mock_devices[i].virtual_relay_id >= 0) {
            printf("      Mapped to Relay: %d\n", mock_devices[i].virtual_relay_id);
        }
        printf("      State: %s", mock_devices[i].current_state ? "ON" : "OFF");
        if (mock_devices[i].brightness > 0) {
            printf(", Brightness: %d", mock_devices[i].brightness);
        }
        printf("\n");
    }
    printf("================================\n\n");
}
