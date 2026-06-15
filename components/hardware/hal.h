#ifndef HAL_H
#define HAL_H

/**
 * @file hal.h
 * @brief Hardware Abstraction Layer for Sentinel alarm system
 * 
 * Provides unified interface for hardware control across:
 * - Zone monitoring (GPIO digital inputs)
 * - Relay control (GPIO digital outputs)
 * - Network configuration (static/DHCP)
 * - RF transceiver (for wireless sensors)
 * 
 * Automatically selects platform-specific implementation:
 * - ESP32-S3: Real GPIO and network drivers
 * - Linux: Mock implementations for testing
 */

#include <stdint.h>
#include <stdbool.h>
#include "storage_mgr.h"

/**
 * @brief Initialize RF transceiver (RMT peripheral)
 * 
 * Sets up RMT (Remote Control) peripheral on GPIO4 for
 * receiving 433MHz wireless sensor signals.
 */
void rf_init(void);

/**
 * @brief Send RF code (for testing/pairing)
 * 
 * Transmits 433MHz wireless signal. Used for pairing
 * wireless sensors with the alarm system.
 * 
 * @param code 32-bit RF command code
 */
void rf_send_code(uint32_t code);

/**
 * @brief Last received RF code
 * 
 * Global variable tracking the most recent wireless signal received.
 * Used by application logic to detect wireless sensor triggers.
 */
extern uint32_t last_rf_code;

/**
 * @brief Initialize hardware (GPIO setup)
 * 
 * Configures all I2C and SPI peripherals. Called during app_main()
 * before zone/relay initialization.
 */
void hal_esp32_init(void);

/**
 * @brief Configure zones (GPIO inputs) for monitoring
 * 
 * Sets up GPIO input pins for zone sensors with pull-up resistors.
 * Zones use active-low logic: 1=secure, 0=violated.
 * 
 * @param zones Array of zone configurations
 * @param count Number of zones to initialize
 */
void hal_init_zones(zone_t *zones, int count);

/**
 * @brief Read zone state (door/window sensor)
 * 
 * Reads GPIO input pin for zone. Returns pull-up logic state.
 * Includes validation to prevent invalid GPIO access.
 * 
 * @param pin GPIO pin number (0-49 on ESP32-S3)
 * @return int 1 if secure (circuit open), 0 if violated (circuit closed)
 */
int hal_get_zone_state(int pin);

/**
 * @brief Control relay output (alarm, siren, light, etc.)
 * 
 * Sets GPIO output pin for relay control. Verifies index and GPIO
 * validity (0-7 index, 0-49 GPIO). Uses KC868-A8V3 hardcoded relay pins.
 * 
 * @param index Relay index (0-7)
 * @param state 1 to activate, 0 to deactivate
 */
void hal_set_relay(int index, int state);
bool hal_get_relay_state(int gpio);

/**
 * @brief Activate/deactivate alarm siren
 * 
 * Convenience function - maps to relay index 0 (siren relay).
 * 
 * @param state 1 to sound siren, 0 to stop
 */
void hal_set_siren(int state);

/**
 * @brief Activate/deactivate strobe light
 * 
 * Convenience function - maps to relay index 1 (strobe relay).
 * 
 * @param state 1 to flash light, 0 to stop
 */
void hal_set_strobe(int state);

#endif