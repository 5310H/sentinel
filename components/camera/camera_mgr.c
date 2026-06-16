/**
 * @file camera_mgr.c
 * @brief Camera Management Implementation
 */

#include "camera_mgr.h"
#include "camera_zones.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>

#define TAG "CAMERA_MGR"

// Camera storage paths
#define CAMERA_CONFIG_PATH "/spiffs/cameras.json"
#define CAMERA_CONFIG_PATH_SD "/sd/cameras.json"
#define ALARM_SNAPSHOT_DIR "/sd/alarms"

// HTTP timeout settings
#define HTTP_TIMEOUT_MS 5000
#define MAX_CONSECUTIVE_FAILURES 3

// Static storage for camera devices
static camera_device_t cameras[CAMERA_MAX_DEVICES];
static int camera_count = 0;
static camera_stats_t stats = {0};
static bool initialized = false;
static bool sd_card_available = false;

/**
 * Initialize camera manager
 */
esp_err_t camera_mgr_init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "Camera manager already initialized");
        return ESP_OK;
    }

    memset(cameras, 0, sizeof(cameras));
    camera_count = 0;
    memset(&stats, 0, sizeof(stats));

    // Check if SD card is mounted
    struct stat st = {0};
    if (stat("/sd", &st) == 0 && S_ISDIR(st.st_mode)) {
        sd_card_available = true;
        ESP_LOGI(TAG, "SD card detected and mounted");
        
        // Create alarm snapshot directory if it doesn't exist
        if (stat(ALARM_SNAPSHOT_DIR, &st) == -1) {
            mkdir(ALARM_SNAPSHOT_DIR, 0755);
            ESP_LOGI(TAG, "Created alarm snapshot directory: %s", ALARM_SNAPSHOT_DIR);
        }
    } else {
        sd_card_available = false;
        ESP_LOGW(TAG, "SD card not detected - alarm recording will be disabled");
    }

    initialized = true;
    ESP_LOGI(TAG, "Camera manager initialized (max %d devices)", CAMERA_MAX_DEVICES);
    
    // Initialize camera zones system
    camera_zones_init();
    
    return ESP_OK;
}

/**
 * Load camera configuration from JSON
 */
esp_err_t camera_mgr_load_config(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Camera manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Try SD card first, fallback to SPIFFS
    const char *path = CAMERA_CONFIG_PATH_SD;
    FILE *f = fopen(path, "r");
    if (!f) {
        path = CAMERA_CONFIG_PATH;
        f = fopen(path, "r");
    }

    if (!f) {
        ESP_LOGW(TAG, "No camera config found, starting with empty list");
        return ESP_OK;
    }

    // Read file into buffer
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize > 10240) { // 10KB limit
        ESP_LOGE(TAG, "Config file too large: %ld bytes", fsize);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    char *buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fread(buffer, 1, fsize, f);
    buffer[fsize] = 0;
    fclose(f);

    // Parse JSON
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse camera config JSON");
        return ESP_FAIL;
    }

    if (!cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Camera config must be JSON array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Load camera devices
    camera_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (camera_count >= CAMERA_MAX_DEVICES) {
            ESP_LOGW(TAG, "Max cameras reached, ignoring extras");
            break;
        }

        cJSON *hostname = cJSON_GetObjectItem(item, "hostname");
        cJSON *port = cJSON_GetObjectItem(item, "port");
        cJSON *name = cJSON_GetObjectItem(item, "friendly_name");
        cJSON *esphome_id = cJSON_GetObjectItem(item, "esphome_id");
        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *sd_recording = cJSON_GetObjectItem(item, "sd_recording_enabled");
        cJSON *snapshot_cmd = cJSON_GetObjectItem(item, "snapshot_cmd");

        if (!hostname || !name) {
            ESP_LOGW(TAG, "Skipping camera with missing required fields");
            continue;
        }

        camera_device_t *cam = &cameras[camera_count];
        strncpy(cam->hostname, hostname->valuestring, sizeof(cam->hostname) - 1);
        cam->port = port ? port->valueint : 80;
        strncpy(cam->friendly_name, name->valuestring, sizeof(cam->friendly_name) - 1);
        if (esphome_id && cJSON_IsString(esphome_id)) {
            strncpy(cam->esphome_id, esphome_id->valuestring, sizeof(cam->esphome_id) - 1);
        } else {
            strcpy(cam->esphome_id, "camera");
        }
        cam->enabled = enabled ? cJSON_IsTrue(enabled) : true;
        cam->sd_recording_enabled = sd_recording ? cJSON_IsTrue(sd_recording) : sd_card_available;
        
        // Load custom snapshot command if provided
        if (snapshot_cmd && cJSON_IsString(snapshot_cmd)) {
            strncpy(cam->snapshot_cmd, snapshot_cmd->valuestring, sizeof(cam->snapshot_cmd) - 1);
            ESP_LOGI(TAG, "Custom snapshot URL: %s", cam->snapshot_cmd);
        } else {
            cam->snapshot_cmd[0] = '\0'; // Empty means use default patterns
        }
        
        // Load motion detection command if provided
        cJSON *motion_cmd_json = cJSON_GetObjectItem(item, "motion_cmd");
        cJSON *motion_enabled = cJSON_GetObjectItem(item, "motion_detection_enabled");
        if (motion_cmd_json && cJSON_IsString(motion_cmd_json)) {
            strncpy(cam->motion_cmd, motion_cmd_json->valuestring, sizeof(cam->motion_cmd) - 1);
            cam->motion_detection_enabled = motion_enabled ? cJSON_IsTrue(motion_enabled) : true;
            ESP_LOGI(TAG, "Motion detection enabled for %s: %s", cam->friendly_name, cam->motion_cmd);
        } else {
            cam->motion_cmd[0] = '\0';
            cam->motion_detection_enabled = false;
        }
        
        cam->snapshot_buffer = NULL;
        cam->snapshot_size = 0;
        cam->last_snapshot_time = 0;
        cam->motion_detected = false;
        cam->last_motion_time = 0;
        cam->consecutive_failures = 0;

        ESP_LOGI(TAG, "Loaded camera: %s (%s:%d) - %s, SD recording: %s", 
                 cam->friendly_name, cam->hostname, cam->port,
                 cam->enabled ? "enabled" : "disabled",
                 cam->sd_recording_enabled ? "enabled" : "disabled");
        camera_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d camera(s) from %s", camera_count, path);
    return ESP_OK;
}

/**
 * Save camera configuration to JSON
 */
esp_err_t camera_mgr_save_config(void) {
    cJSON *root = cJSON_CreateArray();
    if (!root) return ESP_ERR_NO_MEM;

    for (int i = 0; i < camera_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "hostname", cameras[i].hostname);
        cJSON_AddNumberToObject(item, "port", cameras[i].port);
        cJSON_AddStringToObject(item, "friendly_name", cameras[i].friendly_name);
        cJSON_AddStringToObject(item, "esphome_id", cameras[i].esphome_id);
        cJSON_AddBoolToObject(item, "enabled", cameras[i].enabled);
        cJSON_AddBoolToObject(item, "sd_recording_enabled", cameras[i].sd_recording_enabled);
        
        // Save custom snapshot command if set
        if (strlen(cameras[i].snapshot_cmd) > 0) {
            cJSON_AddStringToObject(item, "snapshot_cmd", cameras[i].snapshot_cmd);
        }
        
        // Save motion detection settings if enabled
        if (cameras[i].motion_detection_enabled && strlen(cameras[i].motion_cmd) > 0) {
            cJSON_AddStringToObject(item, "motion_cmd", cameras[i].motion_cmd);
            cJSON_AddBoolToObject(item, "motion_detection_enabled", cameras[i].motion_detection_enabled);
        }
        
        cJSON_AddItemToArray(root, item);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) return ESP_ERR_NO_MEM;

    // Try SD card first, fallback to SPIFFS
    FILE *f = fopen(CAMERA_CONFIG_PATH_SD, "w");
    if (!f) {
        f = fopen(CAMERA_CONFIG_PATH, "w");
    }

    if (!f) {
        ESP_LOGE(TAG, "Failed to open config file for writing");
        free(json_str);
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_str);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "Saved %d camera(s) to config", camera_count);
    return ESP_OK;
}

/**
 * Add a new camera device
 */
int camera_mgr_add_device(const char *hostname, int port, 
                          const char *friendly_name, 
                          const char *esphome_id,
                          const char *username,
                          const char *password,
                          const char *snapshot_cmd,
                          const char *motion_cmd,
                          bool enabled) {
    if (!initialized) return -1;
    if (camera_count >= CAMERA_MAX_DEVICES) {
        ESP_LOGE(TAG, "Cannot add camera: max limit reached");
        return -1;
    }

    camera_device_t *cam = &cameras[camera_count];
    strncpy(cam->hostname, hostname, sizeof(cam->hostname) - 1);
    cam->port = port > 0 ? port : 80;
    strncpy(cam->friendly_name, friendly_name, sizeof(cam->friendly_name) - 1);
    strncpy(cam->esphome_id, esphome_id ? esphome_id : "camera", sizeof(cam->esphome_id) - 1);
    strncpy(cam->username, username ? username : "", sizeof(cam->username) - 1);
    strncpy(cam->password, password ? password : "", sizeof(cam->password) - 1);
    strncpy(cam->snapshot_cmd, snapshot_cmd ? snapshot_cmd : "", sizeof(cam->snapshot_cmd) - 1);
    strncpy(cam->motion_cmd, motion_cmd ? motion_cmd : "", sizeof(cam->motion_cmd) - 1);
    cam->enabled = enabled;
    cam->sd_recording_enabled = sd_card_available; // Enable by default if SD available
    cam->motion_detection_enabled = (motion_cmd && strlen(motion_cmd) > 0); // Enable if motion_cmd provided
    cam->snapshot_buffer = NULL;
    cam->snapshot_size = 0;
    cam->last_snapshot_time = 0;
    cam->motion_detected = false;
    cam->last_motion_time = 0;
    cam->consecutive_failures = 0;

    int camera_id = camera_count;
    camera_count++;

    // Register camera as a zone if motion detection is enabled
    if (cam->motion_detection_enabled) {
        camera_zones_register_camera(camera_id);
    }

    ESP_LOGI(TAG, "Added camera %d: %s (%s:%d)", camera_id, 
             friendly_name, hostname, cam->port);
    
    camera_mgr_save_config();
    return camera_id;
}

/**
 * Remove a camera device
 */
esp_err_t camera_mgr_remove_device(int camera_id) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    // Free snapshot buffer
    if (cameras[camera_id].snapshot_buffer) {
        free(cameras[camera_id].snapshot_buffer);
    }

    // Shift remaining cameras
    for (int i = camera_id; i < camera_count - 1; i++) {
        cameras[i] = cameras[i + 1];
    }
    camera_count--;

    ESP_LOGI(TAG, "Removed camera %d", camera_id);
    camera_mgr_save_config();
    return ESP_OK;
}

/**
 * Get camera device information
 */
const camera_device_t* camera_mgr_get_device(int camera_id) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return NULL;
    }
    return &cameras[camera_id];
}

/**
 * Get number of cameras
 */
int camera_mgr_get_count(void) {
    return camera_count;
}

/**
 * Poll camera for motion detection status
 * Returns true if motion was detected, false otherwise
 */
bool camera_mgr_poll_motion(int camera_id) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return false;
    }

    camera_device_t *cam = &cameras[camera_id];
    if (!cam->enabled || !cam->motion_detection_enabled || strlen(cam->motion_cmd) == 0) {
        return false;
    }

    ESP_LOGD(TAG, "Polling motion for camera %d: %s", camera_id, cam->motion_cmd);

#ifdef ESP_PLATFORM
    esp_http_client_config_t config = {
        .url = cam->motion_cmd,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000, // 5 second timeout for motion polling
    };

    if (strlen(cam->username) > 0) {
        config.username = cam->username;
        config.password = cam->password;
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
    }

    esp_http_client_handle_t client = esp_http_client_initstorage_get_config();
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for motion polling");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    bool motion_detected = false;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            // Read response body to check for motion status
            char response_buffer[256] = {0};
            int content_length = esp_http_client_get_content_length(client);
            
            if (content_length > 0 && content_length < sizeof(response_buffer)) {
                int read_len = esp_http_client_read(client, response_buffer, sizeof(response_buffer) - 1);
                if (read_len > 0) {
                    response_buffer[read_len] = '\0';
                    
                    // Parse response - look for motion indicators
                    // This is camera-specific, but common patterns:
                    // - JSON: {"motion": true} or {"motion_detected": 1}
                    // - Plain text: "motion" or "1"
                    // - HTTP status based (some cameras return 200 for motion, 204 for no motion)
                    
                    // For now, assume JSON response with "motion" field
                    cJSON *root = cJSON_Parse(response_buffer);
                    if (root) {
                        cJSON *motion = cJSON_GetObjectItem(root, "motion");
                        if (motion) {
                            motion_detected = cJSON_IsTrue(motion);
                        } else {
                            // Try "motion_detected" field
                            motion = cJSON_GetObjectItem(root, "motion_detected");
                            if (motion) {
                                motion_detected = cJSON_IsTrue(motion) || (cJSON_IsNumber(motion) && motion->valueint != 0);
                            }
                        }
                        cJSON_Delete(root);
                    } else {
                        // Not JSON, check for simple text responses
                        motion_detected = (strstr(response_buffer, "true") != NULL || 
                                         strstr(response_buffer, "1") != NULL ||
                                         strstr(response_buffer, "motion") != NULL);
                    }
                    
                    ESP_LOGD(TAG, "Motion poll response: %s -> %s", response_buffer, motion_detected ? "MOTION" : "no motion");
                }
            }
            
            cam->consecutive_failures = 0; // Reset failure count on success
        } else {
            ESP_LOGW(TAG, "Motion poll failed for camera %d: HTTP %d", camera_id, status_code);
            cam->consecutive_failures++;
        }
    } else {
        ESP_LOGW(TAG, "Motion poll network error for camera %d: %s", camera_id, esp_err_to_name(err));
        cam->consecutive_failures++;
    }

    esp_http_client_cleanup(client);

    // Update motion state
    cam->motion_detected = motion_detected;
    
    if (motion_detected) {
        cam->last_motion_time = time(NULL);
        ESP_LOGI(TAG, "Motion detected by camera %d (%s)", camera_id, cam->friendly_name);
    }

    return motion_detected;

#else
    // Linux mock implementation
    // Simulate motion detection for testing
    static time_t last_motion[4] = {0};
    time_t now = time(NULL);
    
    // Simulate random motion every 30-120 seconds
    if (now - last_motion[camera_id] > 30 + (rand() % 90)) {
        last_motion[camera_id] = now;
        cam->motion_detected = true;
        cam->last_motion_time = now;
        ESP_LOGI(TAG, "[MOCK] Motion detected by camera %d (%s)", camera_id, cam->friendly_name);
        return true;
    } else {
        cam->motion_detected = false;
        return false;
    }
#endif
}

/**
 * Get motion detection status for a camera
 */
bool camera_mgr_get_motion_status(int camera_id) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return false;
    }
    return cameras[camera_id].motion_detected;
}

/**
 * Set motion detection enabled for a camera
 */
esp_err_t camera_mgr_set_motion_detection(int camera_id, bool enabled) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    cameras[camera_id].motion_detection_enabled = enabled;
    ESP_LOGI(TAG, "Camera %d motion detection %s", camera_id, enabled ? "enabled" : "disabled");
    
    camera_mgr_save_config();
    return ESP_OK;
}

/**
 * Fetch snapshot from camera via HTTP
 * Supports both ESPHome and Thingino URL formats
 */
esp_err_t camera_mgr_fetch_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    camera_device_t *cam = &cameras[camera_id];
    
    if (!cam->enabled) {
        ESP_LOGW(TAG, "Camera %d is disabled", camera_id);
        return ESP_ERR_INVALID_STATE;
    }

    // If custom snapshot command is provided, use it first
    if (strlen(cam->snapshot_cmd) > 0) {
        ESP_LOGI(TAG, "Using custom snapshot URL: %s", cam->snapshot_cmd);
        
        esp_http_client_config_t config = {
            .url = cam->snapshot_cmd,
            .method = HTTP_METHOD_GET,
            .timeout_ms = HTTP_TIMEOUT_MS,
        };

        esp_http_client_handle_t client = esp_http_client_initstorage_get_config();
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client for custom URL");
            return ESP_FAIL;
        }

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200) {
                int content_length = esp_http_client_get_content_length(client);
                if (content_length > 0 && content_length <= CAMERA_SNAPSHOT_BUFFER_SIZE) {
                    // Allocate buffer
                    if (!cam->snapshot_buffer || cam->snapshot_size < content_length) {
                        uint8_t *new_buffer = realloc(cam->snapshot_buffer, content_length);
                        if (!new_buffer) {
                            ESP_LOGE(TAG, "Failed to allocate %d bytes for snapshot", content_length);
                            esp_http_client_cleanup(client);
                            return ESP_ERR_NO_MEM;
                        }
                        cam->snapshot_buffer = new_buffer;
                    }

                    // Read data
                    int total_read = 0;
                    while (total_read < content_length) {
                        int read_len = esp_http_client_read(client, 
                                                            (char*)cam->snapshot_buffer + total_read, 
                                                            content_length - total_read);
                        if (read_len <= 0) break;
                        total_read += read_len;
                    }

                    esp_http_client_cleanup(client);

                    if (total_read == content_length) {
                        // Success!
                        cam->snapshot_size = content_length;
                        cam->last_snapshot_time = time(NULL);
                        cam->consecutive_failures = 0;
                        stats.total_snapshots++;

                        *out_buffer = cam->snapshot_buffer;
                        *out_size = cam->snapshot_size;

                        ESP_LOGI(TAG, "Captured %d bytes from camera %d using custom URL", 
                                 content_length, camera_id);
                        return ESP_OK;
                    }
                }
            }
        }
        
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "Custom snapshot URL failed, trying default patterns");
    }

    // Try multiple URL patterns (ESPHome, Thingino, generic)
    const char *url_patterns[] = {
        "http://%s:%d/%s/snapshot",           // ESPHome format
        "http://%s:%d/image.jpg",              // Thingino format
        "http://%s:%d/snapshot.jpg",           // Generic format
        "http://%s:%d/cgi-bin/snapshot.cgi"   // CGI format
    };
    
    for (int pattern_idx = 0; pattern_idx < 4; pattern_idx++) {
        char url[256];
        
        if (pattern_idx == 0) {
            // ESPHome format - use esphome_id
            snprintf(url, sizeof(url), url_patterns[pattern_idx], 
                     cam->hostname, cam->port, cam->esphome_id);
        } else {
            // Other formats - no esphome_id needed
            snprintf(url, sizeof(url), url_patterns[pattern_idx], 
                     cam->hostname, cam->port);
        }

        ESP_LOGI(TAG, "Trying URL pattern %d: %s", pattern_idx + 1, url);

        // Configure HTTP client
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = HTTP_TIMEOUT_MS,
        };

        esp_http_client_handle_t client = esp_http_client_initstorage_get_config();
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client");
            continue;
        }

        // Perform HTTP request
        esp_err_t err = esp_http_client_perform(client);
        
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP request failed for pattern %d: %s", 
                     pattern_idx + 1, esp_err_to_name(err));
            esp_http_client_cleanup(client);
            continue;
        }

        int status_code = esp_http_client_get_status_code(client);
        if (status_code != 200) {
            ESP_LOGW(TAG, "HTTP error %d from pattern %d", status_code, pattern_idx + 1);
            esp_http_client_cleanup(client);
            continue;
        }

        // Get content length
        int content_length = esp_http_client_get_content_length(client);
        if (content_length <= 0 || content_length > CAMERA_SNAPSHOT_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Invalid content length: %d", content_length);
            esp_http_client_cleanup(client);
            continue;
        }

        // Allocate/reallocate buffer
        if (!cam->snapshot_buffer || cam->snapshot_size < content_length) {
            uint8_t *new_buffer = realloc(cam->snapshot_buffer, content_length);
            if (!new_buffer) {
                ESP_LOGE(TAG, "Failed to allocate %d bytes for snapshot", content_length);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            cam->snapshot_buffer = new_buffer;
        }

        // Read response data
        int total_read = 0;
        while (total_read < content_length) {
            int read_len = esp_http_client_read(client, 
                                                (char*)cam->snapshot_buffer + total_read, 
                                                content_length - total_read);
            if (read_len <= 0) break;
            total_read += read_len;
        }

        esp_http_client_cleanup(client);

        if (total_read != content_length) {
            ESP_LOGW(TAG, "Incomplete read: %d/%d bytes", total_read, content_length);
            continue;
        }

        // Success! Update camera state
        cam->snapshot_size = content_length;
        cam->last_snapshot_time = time(NULL);
        cam->consecutive_failures = 0;
        stats.total_snapshots++;

        *out_buffer = cam->snapshot_buffer;
        *out_size = cam->snapshot_size;

        ESP_LOGI(TAG, "Captured %d bytes from camera %d using pattern %d", 
                 content_length, camera_id, pattern_idx + 1);
        return ESP_OK;
    }
    
    // All patterns failed
    ESP_LOGE(TAG, "All URL patterns failed for camera %d", camera_id);
    cam->consecutive_failures++;
    stats.failed_snapshots++;
    return ESP_FAIL;
}

/**
 * Get cached snapshot
 */
esp_err_t camera_mgr_get_cached_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    camera_device_t *cam = &cameras[camera_id];
    
    if (!cam->snapshot_buffer || cam->snapshot_size == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_buffer = cam->snapshot_buffer;
    *out_size = cam->snapshot_size;
    return ESP_OK;
}

/**
 * Get RTSP stream URL
 */
esp_err_t camera_mgr_get_stream_url(int camera_id, char *url, size_t url_len) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    camera_device_t *cam = &cameras[camera_id];
    snprintf(url, url_len, "rtsp://%s:554/%s", cam->hostname, cam->esphome_id);
    return ESP_OK;
}

/**
 * Save snapshot to SD card
 */
esp_err_t camera_mgr_save_snapshot_to_sd(int camera_id, int zone_id, 
                                         const uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if SD card is available
    if (!sd_card_available) {
        ESP_LOGW(TAG, "SD card not available, skipping snapshot save");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if SD recording is enabled for this camera
    if (camera_id >= 0 && camera_id < camera_count) {
        if (!cameras[camera_id].sd_recording_enabled) {
            ESP_LOGD(TAG, "SD recording disabled for camera %d, skipping", camera_id);
            return ESP_OK; // Not an error, just disabled
        }
    }

    // Generate filename: /sd/alarms/TIMESTAMP_zoneXX_camX.jpg
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char filename[128];
    snprintf(filename, sizeof(filename), 
             "%s/%04d%02d%02d_%02d%02d%02d_zone%02d_cam%d.jpg",
             ALARM_SNAPSHOT_DIR,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             zone_id, camera_id);

    FILE *f = fopen(filename, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", filename);
        return ESP_FAIL;
    }

    size_t written = fwrite(buffer, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Failed to write complete snapshot: %d/%d bytes", written, size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved alarm snapshot: %s (%d bytes)", filename, size);
    return ESP_OK;
}

/**
 * Trigger alarm snapshots from all enabled cameras
 */
int camera_mgr_trigger_alarm_snapshots(int zone_id) {
    int captured = 0;

    ESP_LOGI(TAG, "Triggering alarm snapshots for zone %d", zone_id);

    for (int i = 0; i < camera_count; i++) {
        if (!cameras[i].enabled) continue;

        uint8_t *buffer = NULL;
        size_t size = 0;

        if (camera_mgr_fetch_snapshot(i, &buffer, &size) == ESP_OK) {
            if (camera_mgr_save_snapshot_to_sd(i, zone_id, buffer, size) == ESP_OK) {
                captured++;
                stats.alarm_triggered_snapshots++;
                stats.last_alarm_snapshot = time(NULL);
            }
        }
    }

    ESP_LOGI(TAG, "Captured %d alarm snapshots", captured);
    return captured;
}

/**
 * Enable or disable camera
 */
esp_err_t camera_mgr_set_enabled(int camera_id, bool enabled) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    cameras[camera_id].enabled = enabled;
    ESP_LOGI(TAG, "Camera %d %s", camera_id, enabled ? "enabled" : "disabled");
    
    camera_mgr_save_config();
    return ESP_OK;
}

/**
 * Get camera statistics
 */
esp_err_t camera_mgr_get_stats(camera_stats_t *out_stats) {
    if (!out_stats) return ESP_ERR_INVALID_ARG;
    memcpy(out_stats, &stats, sizeof(camera_stats_t));
    return ESP_OK;
}

/**
 * Test camera connection
 */
esp_err_t camera_mgr_test_connection(int camera_id) {
    uint8_t *buffer = NULL;
    size_t size = 0;
    return camera_mgr_fetch_snapshot(camera_id, &buffer, &size);
}

/**
 * Check if SD card is available
 */
bool camera_mgr_is_sd_available(void) {
    return sd_card_available;
}

/**
 * Set SD recording enabled for a camera
 */
esp_err_t camera_mgr_set_sd_recording(int camera_id, bool enabled) {
    if (camera_id < 0 || camera_id >= camera_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (enabled && !sd_card_available) {
        ESP_LOGW(TAG, "Cannot enable SD recording: SD card not available");
        return ESP_ERR_INVALID_STATE;
    }

    cameras[camera_id].sd_recording_enabled = enabled;
    ESP_LOGI(TAG, "Camera %d SD recording %s", camera_id, enabled ? "enabled" : "disabled");
    
    camera_mgr_save_config();
    return ESP_OK;
}
