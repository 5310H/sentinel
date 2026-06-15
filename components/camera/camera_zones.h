/**
 * @file camera_zones.h
 * @brief Camera Zone Integration Header
 *
 * Provides camera-based zone functionality for motion detection alarm triggering.
 */

#ifndef CAMERA_ZONES_H
#define CAMERA_ZONES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NOT_FOUND -4
#define ESP_ERR_NO_MEM -5
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize camera zones system
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_zones_init(void);

/**
 * @brief Register a camera as a zone
 *
 * Associates a camera with a zone index for motion detection alarm triggering.
 * Automatically assigns a zone index for the camera.
 *
 * @param camera_id Camera ID to monitor
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_zones_register_camera(int camera_id);

/**
 * @brief Unregister a camera zone
 *
 * @param camera_id Camera ID to unregister
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not registered
 */
esp_err_t camera_zones_unregister_camera(int camera_id);

/**
 * @brief Poll all registered camera zones for motion
 *
 * Should be called periodically to check cameras for motion detection.
 * When motion is detected on armed systems, triggers zone violations.
 */
void camera_zones_poll_all(void);

/**
 * @brief Get zone index for a camera
 *
 * @param camera_id Camera ID
 * @param zone_index Output zone index (can be NULL)
 * @return true if camera is registered as zone, false otherwise
 */
bool camera_zones_get_info(int camera_id, int *zone_index);

/**
 * @brief Check if zone index is a camera zone
 *
 * @param zone_index Zone index to check
 * @return true if zone is camera-based, false otherwise
 */
bool camera_zones_is_camera_zone(int zone_index);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_ZONES_H