
#include "camera_mgr.h"
#include <stddef.h>
#include <stdbool.h>

// Mock camera storage
static camera_device_t mock_cameras[CAMERA_MAX_DEVICES];
static int mock_camera_count = 0;
static camera_stats_t mock_stats = {0};
static bool mock_initialized = false;
bool g_loading_config = false;

const camera_device_t* camera_mgr_get_device(int camera_id) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return NULL;
    }
    return &mock_cameras[camera_id];
}
/**
 * @file camera_mock.c
 * @brief Mock implementation of camera manager for Linux testing
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "camera_mgr.h"
#include "cJSON.h"
#include <stdbool.h>
// ...existing code...
#define TAG "CAMERA_MOCK"

// Mock camera storage

// Mock snapshot data (1x1 pixel JPEG - smallest valid JPEG)
static const uint8_t MOCK_JPEG[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
    0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
    0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12,
    0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20,
    0x24, 0x2E, 0x27, 0x20, 0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29,
    0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32,
    0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00,
    0x37, 0xFF, 0xD9
};
static const size_t MOCK_JPEG_SIZE = sizeof(MOCK_JPEG);

/**
 * Initialize camera manager
 */
int camera_mgr_init(void) {
    if (mock_initialized) {
        printf("[%s] Camera manager already initialized\n", TAG);
        return 0; // ESP_OK
    }

    memset(mock_cameras, 0, sizeof(mock_cameras));
    mock_camera_count = 0;
    memset(&mock_stats, 0, sizeof(mock_stats));
    mock_initialized = true;

    printf("[%s] Camera manager initialized (max %d devices)\n", TAG, CAMERA_MAX_DEVICES);
    return 0; // ESP_OK
}

/**
 * Load camera configuration from JSON
 */
int camera_mgr_load_config(void) {
    if (!mock_initialized) {
        printf("[%s] Camera manager not initialized\n", TAG);
        return -1; // ESP_ERR_INVALID_STATE
    }

    FILE *f = fopen("data/cameras.json", "r");
    g_loading_config = true;
    int loaded = 0;
    cJSON *root = NULL;
    if (!f) {
        f = fopen("cameras.json", "r");
        if (!f) {
            printf("[%s] No camera config found, starting with empty list\n", TAG);
            return 0; // ESP_OK
        }
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return -1;
    }
    fread(buffer, 1, fsize, f);
    buffer[fsize] = 0;
    fclose(f);
    root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        printf("[%s] Failed to parse cameras.json\n", TAG);
        return -1;
    }

    cJSON *camera = NULL;
    cJSON_ArrayForEach(camera, root) {
        cJSON *hostname = cJSON_GetObjectItem(camera, "hostname");
        cJSON *port = cJSON_GetObjectItem(camera, "port");
        cJSON *name = cJSON_GetObjectItem(camera, "friendly_name");
        cJSON *camera_id = cJSON_GetObjectItem(camera, "camera_id");
        cJSON *username = cJSON_GetObjectItem(camera, "username");
        cJSON *password = cJSON_GetObjectItem(camera, "password");
        cJSON *enabled = cJSON_GetObjectItem(camera, "enabled");
        cJSON *snapshot_cmd = cJSON_GetObjectItem(camera, "snapshot_cmd");
        cJSON *motion_cmd = cJSON_GetObjectItem(camera, "motion_cmd");
        cJSON *protocol = cJSON_GetObjectItem(camera, "protocol");
        cJSON *stream_url = cJSON_GetObjectItem(camera, "stream_url");
        cJSON *snapshot_url = cJSON_GetObjectItem(camera, "snapshot_url");

        printf("[DEBUG] Loading camera config: hostname=%s, port=%d, name=%s\n",
            hostname && cJSON_IsString(hostname) ? hostname->valuestring : "(null)",
            port && cJSON_IsNumber(port) ? port->valueint : -1,
            name && cJSON_IsString(name) ? name->valuestring : "(null)");
        if (snapshot_cmd && cJSON_IsString(snapshot_cmd)) {
            printf("[DEBUG] snapshot_cmd: %s\n", snapshot_cmd->valuestring);
        }

        if (hostname && cJSON_IsString(hostname) && name && cJSON_IsString(name)) {
            if (!(snapshot_cmd && cJSON_IsString(snapshot_cmd) && strlen(snapshot_cmd->valuestring) > 0)) {
                printf("[ERROR] Camera config for '%s' missing snapshot_cmd. Skipping this camera.\n", name->valuestring);
                continue;
            }
            int cam_port = (port && cJSON_IsNumber(port)) ? port->valueint : 80;
            const char *cam_camera_id = (camera_id && cJSON_IsString(camera_id)) ? camera_id->valuestring : "image.jpg";
            const char *cam_username = (username && cJSON_IsString(username)) ? username->valuestring : "";
            const char *cam_password = (password && cJSON_IsString(password)) ? password->valuestring : "";
            const char *cam_motion_cmd = (motion_cmd && cJSON_IsString(motion_cmd)) ? motion_cmd->valuestring : "";
            bool cam_enabled = (enabled && cJSON_IsBool(enabled)) ? cJSON_IsTrue(enabled) : true;
            int id = camera_mgr_add_device(hostname->valuestring, cam_port, name->valuestring, cam_camera_id, cam_username, cam_password, snapshot_cmd->valuestring, cam_motion_cmd, cam_enabled);
            if (id >= 0) {
                strncpy(mock_cameras[id].snapshot_cmd, snapshot_cmd->valuestring, sizeof(mock_cameras[id].snapshot_cmd)-1);
                printf("[DEBUG] Assigned snapshot_cmd to camera %d: %s\n", id, mock_cameras[id].snapshot_cmd);
                if (protocol && cJSON_IsString(protocol)) {
                    strncpy(mock_cameras[id].protocol, protocol->valuestring, sizeof(mock_cameras[id].protocol)-1);
                } else {
                    mock_cameras[id].protocol[0] = '\0';
                }
                if (stream_url && cJSON_IsString(stream_url)) {
                    strncpy(mock_cameras[id].stream_url, stream_url->valuestring, sizeof(mock_cameras[id].stream_url)-1);
                    printf("[MOCK] Camera %d stream_url: %s\n", id, mock_cameras[id].stream_url);
                }
                if (snapshot_url && cJSON_IsString(snapshot_url)) {
                    printf("[MOCK] Camera %d snapshot_url: %s\n", id, snapshot_url->valuestring);
                }
            }
            loaded++;
        }
    }
    cJSON_Delete(root);
    printf("[%s] Loaded %d camera(s) from config\n", TAG, loaded);
    g_loading_config = false;
    return 0; // ESP_OK
}

/**
 * Save camera configuration
 */
int camera_mgr_save_config(void) {
    cJSON *root = cJSON_CreateArray();
    
    for (int i = 0; i < mock_camera_count; i++) {
        cJSON *cam = cJSON_CreateObject();
        cJSON_AddStringToObject(cam, "hostname", mock_cameras[i].hostname);
        cJSON_AddNumberToObject(cam, "port", mock_cameras[i].port);
        cJSON_AddStringToObject(cam, "friendly_name", mock_cameras[i].friendly_name);
        cJSON_AddStringToObject(cam, "camera_id", mock_cameras[i].camera_id);
        if (strlen(mock_cameras[i].username) > 0) {
            cJSON_AddStringToObject(cam, "username", mock_cameras[i].username);
        }
        if (strlen(mock_cameras[i].password) > 0) {
            cJSON_AddStringToObject(cam, "password", mock_cameras[i].password);
        }
        cJSON_AddBoolToObject(cam, "enabled", mock_cameras[i].enabled);
        if (strlen(mock_cameras[i].snapshot_cmd) > 0) {
            cJSON_AddStringToObject(cam, "snapshot_cmd", mock_cameras[i].snapshot_cmd);
        }
        if (strlen(mock_cameras[i].protocol) > 0) {
            cJSON_AddStringToObject(cam, "protocol", mock_cameras[i].protocol);
        }
        cJSON_AddItemToArray(root, cam);
    }
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        printf("[%s] Failed to generate JSON\n", TAG);
        return -1;
    }
    
    FILE *f = fopen("data/cameras.json", "w");
    if (!f) {
        f = fopen("cameras.json", "w");
        if (!f) {
            printf("[%s] Failed to open cameras.json for writing\n", TAG);
            free(json_str);
            return -1;
        }
    }
    
    fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
    free(json_str);
    
    printf("[%s] Saved %d camera(s) to config\n", TAG, mock_camera_count);
    return 0; // ESP_OK
}

/**
 * Add a camera device
 */
int camera_mgr_add_device(const char *hostname, int port, 
                          const char *friendly_name, 
                          const char *camera_id,
                          const char *username, const char *password,
                          const char *snapshot_cmd,
                          const char *motion_cmd,
                          bool enabled) {
    if (!mock_initialized) return -1;
    if (mock_camera_count >= CAMERA_MAX_DEVICES) {
        printf("[%s] Cannot add camera: max limit reached\n", TAG);
        return -1;
    }

    camera_device_t *cam = &mock_cameras[mock_camera_count];
    strncpy(cam->hostname, hostname, sizeof(cam->hostname) - 1);
    cam->port = port > 0 ? port : 80;
    strncpy(cam->friendly_name, friendly_name, sizeof(cam->friendly_name) - 1);
    strncpy(cam->camera_id, camera_id ? camera_id : "image.jpg", sizeof(cam->camera_id) - 1);
    strncpy(cam->username, username ? username : "", sizeof(cam->username) - 1);
    strncpy(cam->password, password ? password : "", sizeof(cam->password) - 1);
    strncpy(cam->snapshot_cmd, snapshot_cmd ? snapshot_cmd : "", sizeof(cam->snapshot_cmd) - 1);
    cam->enabled = enabled;
    // snapshot_cmd and protocol will be set by config loader if present
    cam->snapshot_buffer = NULL;
    cam->snapshot_size = 0;
    cam->last_snapshot_time = 0;
    cam->consecutive_failures = 0;

        int cam_index = mock_camera_count;
        mock_camera_count++;

        printf("[%s] Added camera %d: %s (%s:%d)\n", TAG, cam_index, 
            friendly_name, hostname, cam->port);

        // Only auto-save if not during initial config load
        extern bool g_loading_config;
        if (!g_loading_config) {
            camera_mgr_save_config();
        }

        return cam_index;
}

/**
 * Remove a camera device
 */
int camera_mgr_remove_device(int camera_id) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return -1;
    }

    // Free snapshot buffer
    if (mock_cameras[camera_id].snapshot_buffer) {
        free(mock_cameras[camera_id].snapshot_buffer);
    }

    // Shift remaining cameras
    for (int i = camera_id; i < mock_camera_count - 1; i++) {
        mock_cameras[i] = mock_cameras[i + 1];
    }
    mock_camera_count--;

    printf("[%s] Removed camera %d\n", TAG, camera_id);
    
    // Only auto-save if not during initial config load
    extern bool g_loading_config;
    if (!g_loading_config) {
        camera_mgr_save_config();
    }
    
    return 0; // ESP_OK
}

/**
 * Get camera device information
 */

/**
 * Get camera count
 */
int camera_mgr_get_count(void) {
    return mock_camera_count;
}

/**
 * Fetch snapshot (mock - returns fake JPEG)
 */
int camera_mgr_fetch_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        printf("[DEBUG] camera_mgr_fetch_snapshot: invalid camera_id %d\n", camera_id);
        return -1;
    }

    camera_device_t *cam = &mock_cameras[camera_id];
    printf("[DEBUG] camera_mgr_fetch_snapshot: camera_id=%d, enabled=%d, snapshot_cmd='%s'\n", camera_id, cam->enabled, cam->snapshot_cmd);
    if (!cam->enabled) {
        printf("[%s] Camera %d is disabled\n", TAG, camera_id);
        return -1;
    }

    // Try to fetch snapshot from camera if snapshot_url is set
    if (strlen(cam->snapshot_cmd) > 0 && strstr(cam->snapshot_cmd, "http://") == cam->snapshot_cmd) {
        // Use snapshot_cmd as the URL (for mock, treat as snapshot_url)
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "curl -s --max-time 5 '%s'", cam->snapshot_cmd);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            uint8_t *buf = malloc(512*1024); // up to 512KB
            if (buf) {
                size_t n = fread(buf, 1, 512*1024, fp);
                pclose(fp);
                if (n > 0) {
                    if (cam->snapshot_buffer) free(cam->snapshot_buffer);
                    cam->snapshot_buffer = buf;
                    cam->snapshot_size = n;
                    cam->last_snapshot_time = time(NULL);
                    cam->consecutive_failures = 0;
                    mock_stats.total_snapshots++;
                    *out_buffer = cam->snapshot_buffer;
                    *out_size = cam->snapshot_size;
                    printf("[%s] Captured %zu bytes from camera %d (HTTP fetch)\n", TAG, n, camera_id);
                    return 0;
                }
                free(buf);
            } else {
                pclose(fp);
            }
        }
        printf("[%s] HTTP fetch failed for camera %d, falling back to mock JPEG\n", TAG, camera_id);
    }
    // Fallback: serve in-memory mock JPEG
    if (!cam->snapshot_buffer) {
        cam->snapshot_buffer = malloc(MOCK_JPEG_SIZE);
        if (!cam->snapshot_buffer) {
            return -1;
        }
    }
    memcpy(cam->snapshot_buffer, MOCK_JPEG, MOCK_JPEG_SIZE);
    cam->snapshot_size = MOCK_JPEG_SIZE;
    cam->last_snapshot_time = time(NULL);
    cam->consecutive_failures = 0;
    mock_stats.total_snapshots++;
    *out_buffer = cam->snapshot_buffer;
    *out_size = cam->snapshot_size;
    printf("[%s] Captured %zu bytes from camera %d (mock fallback)\n", TAG, MOCK_JPEG_SIZE, camera_id);
    return 0; // ESP_OK
}

/**
 * Get cached snapshot
 */
int camera_mgr_get_cached_snapshot(int camera_id, uint8_t **out_buffer, size_t *out_size) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return -1;
    }

    camera_device_t *cam = &mock_cameras[camera_id];
    
    if (!cam->snapshot_buffer || cam->snapshot_size == 0) {
        return -1; // ESP_ERR_NOT_FOUND
    }

    *out_buffer = cam->snapshot_buffer;
    *out_size = cam->snapshot_size;
    return 0; // ESP_OK
}

/**
 * Get RTSP stream URL
 */
int camera_mgr_get_stream_url(int camera_id, char *url, size_t url_len) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return -1;
    }

    camera_device_t *cam = &mock_cameras[camera_id];
    snprintf(url, url_len, "rtsp://%s:554/%s", cam->hostname, cam->camera_id);
    return 0; // ESP_OK
}

/**
 * Save snapshot to disk (mock - just logs)
 */
int camera_mgr_save_snapshot_to_sd(int camera_id, int zone_id, 
                                   const uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }

    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char filename[128];
    snprintf(filename, sizeof(filename), 
             "alarms/%04d%02d%02d_%02d%02d%02d_zone%02d_cam%d.jpg",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             zone_id, camera_id);

    printf("[%s] Mock saved alarm snapshot: %s (%zu bytes)\n", TAG, filename, size);
    return 0; // ESP_OK
}

/**
 * Trigger alarm snapshots
 */
int camera_mgr_trigger_alarm_snapshots(int zone_id) {
    int captured = 0;

    printf("[%s] Triggering alarm snapshots for zone %d\n", TAG, zone_id);

    for (int i = 0; i < mock_camera_count; i++) {
        if (!mock_cameras[i].enabled) continue;

        uint8_t *buffer = NULL;
        size_t size = 0;

        if (camera_mgr_fetch_snapshot(i, &buffer, &size) == 0) {
            if (camera_mgr_save_snapshot_to_sd(i, zone_id, buffer, size) == 0) {
                captured++;
                mock_stats.alarm_triggered_snapshots++;
                mock_stats.last_alarm_snapshot = time(NULL);
            }
        }
    }

    printf("[%s] Captured %d alarm snapshots\n", TAG, captured);
    return captured;
}

/**
 * Enable/disable camera
 */
int camera_mgr_set_enabled(int camera_id, bool enabled) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return -1;
    }

    mock_cameras[camera_id].enabled = enabled;
    printf("[%s] Camera %d %s\n", TAG, camera_id, enabled ? "enabled" : "disabled");
    return 0; // ESP_OK
}

/**
 * Get camera statistics
 */
int camera_mgr_get_stats(camera_stats_t *out_stats) {
    if (!out_stats) return -1;
    memcpy(out_stats, &mock_stats, sizeof(camera_stats_t));
    return 0; // ESP_OK
}

/**
 * Test camera connection
 */
int camera_mgr_test_connection(int camera_id) {
    uint8_t *buffer = NULL;
    size_t size = 0;
    return camera_mgr_fetch_snapshot(camera_id, &buffer, &size);
}

/**
 * Check if SD card is available
 */
bool camera_mgr_is_sd_available(void) {
    // Mock always returns false (no SD card in test environment)
    return false;
}

/**
 * Poll camera for motion detection status
 */
bool camera_mgr_poll_motion(int camera_id) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return false;
    }

    camera_device_t *cam = &mock_cameras[camera_id];
    if (!cam->enabled || !cam->motion_detection_enabled) {
        return false;
    }

    // Mock motion detection - simulate random motion every few polls
    static int poll_count = 0;
    poll_count++;
    
    // Simulate motion detection roughly every 10-20 polls (30 seconds to 1 minute at 50ms intervals)
    bool motion = ((poll_count % 15) == 0);
    
    if (motion) {
        cam->motion_detected = true;
        cam->last_motion_time = time(NULL);
        printf("[CAMERA_MOCK] Motion detected on camera %d\n", camera_id);
    } else {
        cam->motion_detected = false;
    }

    return motion;
}

/**
 * Get current motion detection status for camera
 */
bool camera_mgr_get_motion_status(int camera_id) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return false;
    }
    
    return mock_cameras[camera_id].motion_detected;
}

/**
 * Enable/disable motion detection for camera
 */
int camera_mgr_set_motion_detection(int camera_id, bool enabled) {
    if (camera_id < 0 || camera_id >= mock_camera_count) {
        return -1;
    }
    
    mock_cameras[camera_id].motion_detection_enabled = enabled;
    printf("[CAMERA_MOCK] Motion detection %s for camera %d\n", 
           enabled ? "enabled" : "disabled", camera_id);
    return 0;
}
