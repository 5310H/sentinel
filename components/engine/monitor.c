#include "monitor.h"
#include "camera_zones.h"
#include "engine.h"
#include "storage_mgr.h"
#include <stdio.h>

#include "../esphome_api/esphome_zones.h"
#include "../tuya/tuya_zones.h"

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_log.h"
static const char *TAG = "MONITOR";
#else
#define TAG "MONITOR"
#endif

#define DEBOUNCE_THRESHOLD 3

// --- INITIALIZATION ---

void monitor_init(void) {
  // On ESP32, we assume engine_init has already configured the GPIO directions
  printf("[%s] Service initialized. Scanning %d zones.\n", TAG,
         storage_get_zone_count());
}

// --- EVENT PROCESSING ---

void monitor_process_event(int index) {
  if (index < 0 || index >= storage_get_zone_count())
    return;

  // Direct hand-off to the engine.
  // The engine's tick logic will handle delays (Entry/Exit)
  // or immediate triggers (Fire/Panic).
  engine_process_zone_trip(index);
}

// --- HARDWARE SCANNING ---

void monitor_scan_all() {
  // 1. Scan GPIO-based zones
  for (int i = 0; i < storage_get_zone_count(); i++) {
    storage_lock();
    int gpio = storage_get_zone(i)->gpio;
    bool is_i2c = storage_get_zone(i)->is_i2c;
    int i2c_address = storage_get_zone(i)->i2c_address;
    storage_unlock();
    (void)i2c_address; // Suppress unused warning since hal_read_i2c_adc is commented out

    // Skip virtual zones (gpio = -1) - handled separately
    if (gpio == -1)
      continue;

    int state = 1;

#ifdef ESP_PLATFORM
    if (is_i2c) {
      // --- EOL ADC LOGIC ---
      // Example: Read from external ADS1115 using i2c_address and gpio as
      // channel int adc_val =
      // hal_read_i2c_adc(i2c_address, gpio);
      int adc_val = 1650; // Replace with actual hal_read_i2c_adc() call

      // Thresholds for a 12-bit ADC (0-4095) running at 3.3V
      if (adc_val < 500) {
        // 0V - Wire Shorted! (Tamper)
        if (engine_get_arm_state() != 4) {
          ESP_LOGE(TAG, "EOL TAMPER DETECTED on Zone %d", i);
          engine_trigger_alarm(0); // Trigger physical alarm immediately
        }
        continue; // Skip standard processing
      } else if (adc_val > 3500) {
        // 3.3V - Wire Cut / Door Open (Alarm)
        state = 0;
      } else {
        // ~1.65V - Resistor balanced (Secure)
        state = 1;
      }
    } else {
      state = gpio_get_level((gpio_num_t)gpio);
    }
#else
    // Forward to the mock digitalRead in your test environment
    extern int digitalRead(int pin);
    state = digitalRead(gpio);
#endif

    // If state is 0, the circuit is closed (sensor triggered)
    if (state == 0) {
      // We only process if the engine isn't already in an ALARMED state
      // to prevent redundant trigger logs.
      if (engine_get_arm_state() != 4) {
        monitor_process_event(i);
      }
    }
  }

  // 2. Scan virtual zones (ESPHome, Tuya, etc.)
  for (int i = 0; i < storage_get_zone_count(); i++) {
    storage_lock();
    int gpio = storage_get_zone(i)->gpio;
    int id = storage_get_zone(i)->id;
    storage_unlock();

    // Only process virtual zones (gpio = -1)
    if (gpio != -1)
      continue;

    bool triggered = false;

    // Check ESPHome virtual zones (33-64)
    if (esphome_is_zone_id_valid(id)) {
      triggered = esphome_zone_get_state(id);
    }
    // Check Tuya virtual zones (65-96)
    else if (tuya_is_zone_id_valid(id)) {
      triggered = tuya_zone_get_state(id);
    }

    // Add other virtual zone types here (Matter, Zigbee, etc.)

    // If virtual zone is triggered
    if (triggered) {
      // We only process if the engine isn't already in an ALARMED state
      // to prevent redundant trigger logs.
      if (engine_get_arm_state() != 4) {
        monitor_process_event(i);
      }
    }
  }

  // Poll camera zones for motion detection
  camera_zones_poll_all();
}

// --- ESP32 TASK WRAPPER ---
#ifdef ESP_PLATFORM
void monitor_task(void *pvParameters) {
  ESP_LOGI(TAG, "Monitor Task Started on Core 1");
  while (1) {
    monitor_scan_all();
    // Scan every 50ms for high responsiveness
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif