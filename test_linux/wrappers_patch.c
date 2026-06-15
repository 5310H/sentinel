void __wrap_hal_set_relay(int id, bool state) {
    if (id >= 0 && id < 16) relay_states[id] = state;
    printf("[HAL-RELAY] id: %d | STATE: %s\n", id, state ? "ON" : "OFF");
}

void __wrap_hal_set_siren(bool state) {
    // If global siren is ON, we force Relay 1 and 2 to ON for the Dashboard
    relay_states[1] = state;
    relay_states[2] = state;
    printf("[HAL-SIREN] %s (Syncing Relays 1 & 2)\n", state ? "ON" : "OFF");
}
