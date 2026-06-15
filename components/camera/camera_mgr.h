/**
 * @file camera_mgr.h
 * @brief Camera Management for Sentinel Alarm System
 * 
 * Provides camera integration via ESPHome devices with snapshot capture
 * and RTSP streaming support. Designed for ESP32-CAM devices running ESPHome.
 * 
 * Features:
 * - HTTP snapshot fetching from ESPHome cameras
 * - Alarm-triggered snapshot capture
 * - SD card storage for alarm events
 * - RTSP stream URL generation
 * - Multi-camera support (up to 8 devices)
 * 
 * @author Sentinel Team
 * @date January 30, 2026
 */

#ifndef CAMERA_MGR_H
#define CAMERA_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
// Mock ESP error codes for Linux
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
 * Maximum number of cameras supported
 */
#define CAMERA_MAX_DEVICES 4

/**
 * Maximum snapshot buffer size (100KB for JPEG)
 */
#define CAMERA_SNAPSHOT_BUFFER_SIZE (30 * 1024)  // Reduced from 60KB to save RAM

/**
 * Snapshot quality (JPEG compression)
 */
typedef enum {
    CAMERA_QUALITY_LOW = 30,      // High compression, small file
    CAMERA_QUALITY_MEDIUM = 15,   // Balanced
    CAMERA_QUALITY_HIGH = 10,     // Low compression, large file
    CAMERA_QUALITY_MAX = 5        // Minimal compression
} camera_quality_t;

/**
 * Camera device structure
 */
typedef struct {
    char hostname[64];              // IP or mDNS hostname
    int port;                       // HTTP port (default 80)
    char friendly_name[64];         // Display name
    char camera_id[32];             // Camera entity/path ID (e.g., "camera", "image.jpg")
    char esphome_id[64];            // ESPHome camera entity ID
    char username[32];              // HTTP auth username (optional)
    char password[64];              // HTTP auth password (optional)
    bool enabled;                   // Enable/disable camera
    bool sd_recording_enabled;      // Enable SD card recording for alarms
    bool motion_detection_enabled;  // Enable motion detection for alarm triggering
    char snapshot_cmd[128];         // Custom snapshot command (optional) - reduced from 256
    char motion_cmd[128];           // Custom motion detection command/URL (optional) - reduced from 256
    char protocol[16];              // Protocol (e.g., "rtsp", "http")
    char stream_url[128];           // MJPEG stream URL (from config) - reduced from 256
    uint8_t *snapshot_buffer;       // Cached snapshot data
    size_t snapshot_size;           // Size of cached snapshot
    time_t last_snapshot_time;      // Timestamp of last capture
    bool motion_detected;           // Current motion detection state
    time_t last_motion_time;        // Timestamp of last motion detection
    int consecutive_failures;       // Failure count for health monitoring
} camera_device_t;

/**
 * Camera statistics
 */
typedef struct {
    int total_snapshots;
    int failed_snapshots;
    int alarm_triggered_snapshots;
    time_t last_alarm_snapshot;
} camera_stats_t;

/**
 * @brief Initialize camera manager
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_init(void);

/**
 * @brief Load camera configuration from JSON file
 * 
 * Loads cameras from /spiffs/cameras.json or /sd/cameras.json
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_load_config(void);

/**
 * @brief Save camera configuration to JSON file
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_save_config(void);

/**
 * @brief Add a new camera device
 * 
 * @param hostname Camera hostname or IP address
 * @param port HTTP port (typically 80)
 * @param friendly_name Display name for camera
 * @param esphome_id ESPHome camera entity ID
 * @param username HTTP auth username (optional)
 * @param password HTTP auth password (optional)
 * @param snapshot_cmd Custom snapshot command/URL (optional)
 * @param motion_cmd Custom motion detection command/URL (optional)
 * @param enabled Enable camera on creation
 * @return Camera ID on success, -1 on error
 */
int camera_mgr_add_device(const char *hostname, int port, 
                          const char *friendly_name, 
                          const char *esphome_id,
                          const char *username, const char *password,
                          const char *snapshot_cmd,
                          const char *motion_cmd,
                          bool enabled);

/**
 * @brief Remove a camera device
 * 
 * @param camera_id Camera ID to remove
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_remove_device(int camera_id);

/**
 * @brief Get camera device information
 * 
 * @param camera_id Camera ID
 * @return Pointer to camera device or NULL if not found
 */
const camera_device_t* camera_mgr_get_device(int camera_id);

/**
 * @brief Get number of registered cameras
 * 
 * @return Camera count
 */
int camera_mgr_get_count(void);

/**
 * @brief Poll camera for motion detection status
 * 
 * Makes HTTP request to camera's motion detection endpoint
 * and updates internal motion state.
 * 
 * @param camera_id Camera ID
 * @return true if motion detected, false otherwise
 */
bool camera_mgr_poll_motion(int camera_id);

/**
 * @brief Get current motion detection status for camera
 * 
 * @param camera_id Camera ID
 * @return true if motion currently detected, false otherwise
 */
bool camera_mgr_get_motion_status(int camera_id);

/**
 * @brief Enable/disable motion detection for camera
 * 
 * @param camera_id Camera ID
 * @param enabled Enable motion detection
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_set_motion_detection(int camera_id, bool enabled);

/**
 * @brief Fetch snapshot from camera via HTTP
 * 
 * Downloads JPEG image from ESPHome camera endpoint.
 * Result is cached in camera device structure.
 * 
 * @param camera_id Camera ID
 * @param out_buffer Output pointer to snapshot data (do not free)
 * @param out_size Output size of snapshot
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_fetch_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size);

/**
 * @brief Get cached snapshot without fetching new one
 * 
 * @param camera_id Camera ID
 * @param out_buffer Output pointer to snapshot data (do not free)
 * @param out_size Output size of snapshot
 * @return ESP_OK if cache valid, ESP_ERR_NOT_FOUND if no cache
 */
esp_err_t camera_mgr_get_cached_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size);

/**
 * @brief Get RTSP stream URL for camera
 * 
 * Generates RTSP URL for direct streaming to video players
 * 
 * @param camera_id Camera ID
 * @param url Output buffer for URL
 * @param url_len Buffer size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_get_stream_url(int camera_id, char *url, size_t url_len);

/**
 * @brief Trigger alarm snapshots from all enabled cameras
 * 
 * Called when alarm is triggered. Captures snapshots from all cameras
 * and saves them to SD card with timestamp and zone information.
 * 
 * @param zone_id Zone that triggered alarm
 * @return Number of snapshots captured
 */
int camera_mgr_trigger_alarm_snapshots(int zone_id);

/**
 * @brief Save snapshot to SD card
 * 
 * Saves JPEG snapshot to /sd/alarms/ directory with timestamp
 * 
 * @param camera_id Camera ID
 * @param zone_id Zone that triggered alarm (0 if manual)
 * @param buffer Snapshot data
 * @param size Snapshot size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_save_snapshot_to_sd(int camera_id, int zone_id, 
                                         const uint8_t *buffer, size_t size);

/**
 * @brief Enable or disable camera
 * 
 * @param camera_id Camera ID
 * @param enabled Enable state
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_set_enabled(int camera_id, bool enabled);

/**
 * @brief Get camera statistics
 * 
 * @param stats Output statistics structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_get_stats(camera_stats_t *stats);

/**
 * @brief Test camera connection
 * 
 * Attempts to fetch a snapshot to verify camera is reachable
 * 
 * @param camera_id Camera ID
 * @return ESP_OK if camera responds, error code otherwise
 */
esp_err_t camera_mgr_test_connection(int camera_id);

/**
 * @brief Check if SD card is mounted and available
 * 
 * @return true if SD card is accessible, false otherwise
 */
bool camera_mgr_is_sd_available(void);

/**
 * @brief Set SD recording enabled for a camera
 * 
 * @param camera_id Camera ID
 * @param enabled Enable or disable SD recording
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_mgr_set_sd_recording(int camera_id, bool enabled);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_MGR_H
