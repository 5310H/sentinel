/**
 * @file camera_zones.c
 * @brief Camera Zone Integration for Sentinel Alarm System
 *
 * Integrates camera motion detection with the zone-based alarm system.
 * Cameras with motion detection enabled become virtual zones that can trigger
 * alarms.
 *
 * @author Sentinel Team
 * @date February 6, 2026
 */

#include "camera_mgr.h"
#include "engine.h"
#include "storage_mgr.h"
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "CAMERA_ZONES";
#else
#include <stdio.h>
#define TAG "CAMERA_ZONES"
#define ESP_LOGI(tag, ...)                                                     \
  printf("[%s] ", tag);                                                        \
  printf(__VA_ARGS__);                                                         \
  printf("\n")
#define ESP_LOGW(tag, ...)                                                     \
  printf("[%s] WARNING: ", tag);                                               \
  printf(__VA_ARGS__);                                                         \
  printf("\n")
#define ESP_LOGE(tag, ...)                                                     \
  printf("[%s] ERROR: ", tag);                                                 \
  printf(__VA_ARGS__);                                                         \
  printf("\n")
#define ESP_LOGD(tag, ...)                                                     \
  printf("[%s] DEBUG: ", tag);                                                 \
  printf(__VA_ARGS__);                                                         \
  printf("\n")
#endif

#include "camera_zones.h"

// Camera zone mapping
#define MAX_CAMERA_ZONES 2 // Reduced from 4 to save RAM
static struct {
  int camera_id;    // Camera ID that this zone monitors
  int zone_index;   // Zone index in the main zone array
  bool active;      // Whether this camera zone is active
  time_t last_poll; // Last time we polled this camera
} camera_zones[MAX_CAMERA_ZONES] = {0};

static bool initialized = false;

/**
 * Initialize camera zones system
 */
esp_err_t camera_zones_init(void) {
  if (initialized) {
    return ESP_OK;
  }

  memset(camera_zones, 0, sizeof(camera_zones));
  initialized = true;

  ESP_LOGI(TAG, "Camera zones system initialized");
  return ESP_OK;
}

/**
 * Register a camera as a zone
 * This creates a virtual zone that monitors camera motion detection
 */
esp_err_t camera_zones_register_camera(int camera_id) {
  if (!initialized) {
    ESP_LOGE(TAG, "Camera zones not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (camera_id < 0 || camera_id >= camera_mgr_get_count()) {
    ESP_LOGE(TAG, "Invalid camera ID: %d", camera_id);
    return ESP_ERR_INVALID_ARG;
  }

  // Find free slot
  for (int i = 0; i < MAX_CAMERA_ZONES; i++) {
    if (!camera_zones[i].active) {
      camera_zones[i].camera_id = camera_id;
      // Assign virtual zone index starting from 1000 to avoid conflicts
      camera_zones[i].zone_index = 1000 + camera_id;
      camera_zones[i].active = true;
      camera_zones[i].last_poll = 0;

      ESP_LOGI(TAG, "Registered camera %d as zone %d", camera_id,
               camera_zones[i].zone_index);
      return ESP_OK;
    }
  }

  ESP_LOGE(TAG, "No free camera zone slots");
  return ESP_ERR_NO_MEM;
}

/**
 * Unregister a camera zone
 */
esp_err_t camera_zones_unregister_camera(int camera_id) {
  for (int i = 0; i < MAX_CAMERA_ZONES; i++) {
    if (camera_zones[i].active && camera_zones[i].camera_id == camera_id) {
      camera_zones[i].active = false;
      ESP_LOGI(TAG, "Unregistered camera %d from zone %d", camera_id,
               camera_zones[i].zone_index);
      return ESP_OK;
    }
  }

  return ESP_ERR_NOT_FOUND;
}

/**
 * Poll all registered camera zones for motion
 * This should be called periodically (e.g., every 5-10 seconds)
 */
void camera_zones_poll_all(void) {
  if (!initialized) {
    return;
  }

  time_t now = time(NULL);

  for (int i = 0; i < MAX_CAMERA_ZONES; i++) {
    if (!camera_zones[i].active) {
      continue;
    }

    int camera_id = camera_zones[i].camera_id;
    int zone_index = camera_zones[i].zone_index;

    // Poll camera for motion
    bool motion_detected = camera_mgr_poll_motion(camera_id);

    // If motion detected and we haven't triggered recently, trigger alarm
    if (motion_detected) {
      // Check if zone is armed (should be handled by engine)
      // For now, assume camera zones are always active when armed
      if (engine_get_arm_state_safe() == 2 ||
          engine_get_arm_state_safe() == 3) { // Armed away or stay
        ESP_LOGI(TAG, "Camera zone %d triggered by camera %d", zone_index,
                 camera_id);

        // Trigger zone violation
        engine_process_zone_trip(zone_index);
      }
    }

    camera_zones[i].last_poll = now;
  }
}

/**
 * Get camera zone info
 */
bool camera_zones_get_info(int camera_id, int *zone_index) {
  for (int i = 0; i < MAX_CAMERA_ZONES; i++) {
    if (camera_zones[i].active && camera_zones[i].camera_id == camera_id) {
      if (zone_index) {
        *zone_index = camera_zones[i].zone_index;
      }
      return true;
    }
  }
  return false;
}

/**
 * Check if a zone index is a camera zone
 */
bool camera_zones_is_camera_zone(int zone_index) {
  for (int i = 0; i < MAX_CAMERA_ZONES; i++) {
    if (camera_zones[i].active && camera_zones[i].zone_index == zone_index) {
      return true;
    }
  }
  return false;
}