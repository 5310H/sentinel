#include "hal.h"
#include <stdio.h>

// Simple array to track relay states in the Linux simulation
static bool mock_relays[32] = {false};

bool hal_get_zone_state(int zone_id) {
    // Mock: Zones are normally closed (false = safe)
    return false; 
}

void hal_set_relay_state(int relay_id, bool active) {
    if (relay_id >= 0 && relay_id < 32) {
        mock_relays[relay_id] = active;
        printf("[HAL] Relay %d set to %s\n", relay_id, active ? "ON" : "OFF");
    }
}

bool hal_get_relay_state(int relay_id) {
    if (relay_id >= 0 && relay_id < 32) {
        return mock_relays[relay_id];
    }
    return false;
}