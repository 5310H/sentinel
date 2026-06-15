/**
 * @file zigbee_scenes.c
 * @brief Zigbee scene management implementation
 */

#include "zigbee_scenes.h"
#include "zigbee_mgr.h"
#include "zigbee_devices.h"
#include "storage_mgr.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ZIGBEE_SCENES";

// Scene registry
static struct {
    zigbee_scene_t scenes[ZIGBEE_MAX_SCENES];
    int count;
    bool initialized;
} s_registry = {0};

esp_err_t zigbee_scenes_init(void) {
    if (s_registry.initialized) {
        return ESP_OK;
    }
    
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.initialized = true;
    
    if (zigbee_scenes_load() != ESP_OK) {
        ESP_LOGW(TAG, "No saved scenes found, starting fresh");
    }
    
    ESP_LOGI(TAG, "Initialized with %d scenes", s_registry.count);
    return ESP_OK;
}

void zigbee_scenes_deinit(void) {
    if (!s_registry.initialized) {
        return;
    }
    
    zigbee_scenes_save();
    memset(&s_registry, 0, sizeof(s_registry));
}

esp_err_t zigbee_scenes_create(const char *name, uint8_t *scene_id) {
    if (!s_registry.initialized || !name || !scene_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_registry.count >= ZIGBEE_MAX_SCENES) {
        ESP_LOGE(TAG, "Maximum scenes reached (%d)", ZIGBEE_MAX_SCENES);
        return ESP_ERR_NO_MEM;
    }
    
    // Find free scene ID
    uint8_t new_id = 0;
    for (int i = 0; i < ZIGBEE_MAX_SCENES; i++) {
        bool id_used = false;
        for (int j = 0; j < s_registry.count; j++) {
            if (s_registry.scenes[j].scene_id == i) {
                id_used = true;
                break;
            }
        }
        if (!id_used) {
            new_id = i;
            break;
        }
    }
    
    zigbee_scene_t *scene = &s_registry.scenes[s_registry.count];
    scene->scene_id = new_id;
    strncpy(scene->name, name, ZIGBEE_SCENE_NAME_LEN - 1);
    scene->name[ZIGBEE_SCENE_NAME_LEN - 1] = '\0';
    scene->device_count = 0;
    scene->enabled = true;
    
    s_registry.count++;
    *scene_id = new_id;
    
    ESP_LOGI(TAG, "Created scene %d: %s", new_id, name);
    zigbee_scenes_save();
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_delete(uint8_t scene_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int idx = -1;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.scenes[i].scene_id == scene_id) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    for (int i = idx; i < s_registry.count - 1; i++) {
        s_registry.scenes[i] = s_registry.scenes[i + 1];
    }
    s_registry.count--;
    
    ESP_LOGI(TAG, "Deleted scene %d", scene_id);
    zigbee_scenes_save();
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_capture_device(uint8_t scene_id, uint16_t short_addr) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    zigbee_scene_t *scene = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.scenes[i].scene_id == scene_id) {
            scene = &s_registry.scenes[i];
            break;
        }
    }
    
    if (!scene) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get current device state
    const zigbee_device_t *dev = zigbee_mgr_get_device(short_addr);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Find or add device in scene
    zigbee_scene_device_t *scene_dev = NULL;
    for (int i = 0; i < scene->device_count; i++) {
        if (scene->devices[i].short_addr == short_addr) {
            scene_dev = &scene->devices[i];
            break;
        }
    }
    
    if (!scene_dev) {
        if (scene->device_count >= ZIGBEE_MAX_SCENE_DEVICES) {
            ESP_LOGE(TAG, "Scene %d is full", scene_id);
            return ESP_ERR_NO_MEM;
        }
        scene_dev = &scene->devices[scene->device_count++];
    }
    
    // Capture state
    scene_dev->short_addr = short_addr;
    scene_dev->has_on_off = (dev->capabilities & ZIGBEE_CAP_ON_OFF) != 0;
    scene_dev->on_off = dev->current_on_off;
    scene_dev->has_level = (dev->capabilities & ZIGBEE_CAP_LEVEL) != 0;
    scene_dev->level = (dev->current_level * 100) / 254;
    scene_dev->has_color = (dev->capabilities & ZIGBEE_CAP_COLOR) != 0;
    scene_dev->color_rgb = dev->current_color_rgb;
    scene_dev->has_color_temp = true; // Most devices support this
    scene_dev->color_temp = dev->current_color_temp;
    
    ESP_LOGI(TAG, "Captured device 0x%04X state into scene %d", short_addr, scene_id);
    zigbee_scenes_save();
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_remove_device(uint8_t scene_id, uint16_t short_addr) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    zigbee_scene_t *scene = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.scenes[i].scene_id == scene_id) {
            scene = &s_registry.scenes[i];
            break;
        }
    }
    
    if (!scene) {
        return ESP_ERR_NOT_FOUND;
    }
    
    int idx = -1;
    for (int i = 0; i < scene->device_count; i++) {
        if (scene->devices[i].short_addr == short_addr) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    for (int i = idx; i < scene->device_count - 1; i++) {
        scene->devices[i] = scene->devices[i + 1];
    }
    scene->device_count--;
    
    ESP_LOGI(TAG, "Removed device 0x%04X from scene %d", short_addr, scene_id);
    zigbee_scenes_save();
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_recall(uint8_t scene_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const zigbee_scene_t *scene = zigbee_scenes_get(scene_id);
    if (!scene || !scene->enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Recalling scene %d: %s", scene_id, scene->name);
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < scene->device_count; i++) {
        const zigbee_scene_device_t *dev = &scene->devices[i];
        
        // Apply on/off state
        if (dev->has_on_off) {
            if (zigbee_mgr_set_on_off(dev->short_addr, dev->on_off) == ESP_OK) {
                success++;
            } else {
                failed++;
                continue;
            }
        }
        
        // Apply brightness
        if (dev->has_level) {
            zigbee_mgr_set_level(dev->short_addr, dev->level);
        }
        
        // Apply color
        if (dev->has_color) {
            zigbee_mgr_set_color(dev->short_addr, dev->color_rgb);
        }
        
        // Apply color temperature
        if (dev->has_color_temp && dev->color_temp > 0) {
            zigbee_mgr_set_color_temp(dev->short_addr, dev->color_temp);
        }
    }
    
    ESP_LOGI(TAG, "Scene %d recall: %d success, %d failed", scene_id, success, failed);
    
    return (success > 0) ? ESP_OK : ESP_FAIL;
}

const zigbee_scene_t* zigbee_scenes_get(uint8_t scene_id) {
    if (!s_registry.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.scenes[i].scene_id == scene_id) {
            return &s_registry.scenes[i];
        }
    }
    
    return NULL;
}

esp_err_t zigbee_scenes_get_all(zigbee_scene_t *scenes, int *count) {
    if (!s_registry.initialized || !scenes || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(scenes, s_registry.scenes, sizeof(zigbee_scene_t) * s_registry.count);
    *count = s_registry.count;
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_save(void) {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        zigbee_scene_t *scene = &s_registry.scenes[i];
        
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "id", scene->scene_id);
        cJSON_AddStringToObject(s, "name", scene->name);
        cJSON_AddBoolToObject(s, "enabled", scene->enabled);
        
        cJSON *devices = cJSON_CreateArray();
        for (int j = 0; j < scene->device_count; j++) {
            zigbee_scene_device_t *dev = &scene->devices[j];
            cJSON *d = cJSON_CreateObject();
            
            cJSON_AddNumberToObject(d, "addr", dev->short_addr);
            if (dev->has_on_off) {
                cJSON_AddBoolToObject(d, "on", dev->on_off);
            }
            if (dev->has_level) {
                cJSON_AddNumberToObject(d, "level", dev->level);
            }
            if (dev->has_color) {
                cJSON_AddNumberToObject(d, "color", dev->color_rgb);
            }
            if (dev->has_color_temp) {
                cJSON_AddNumberToObject(d, "temp", dev->color_temp);
            }
            
            cJSON_AddItemToArray(devices, d);
        }
        cJSON_AddItemToObject(s, "devices", devices);
        
        cJSON_AddItemToArray(root, s);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    FILE *f = fopen("/spiffs/zigbee_scenes.json", "w");
    if (!f) {
        cJSON_free(json_str);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    fputs(json_str, f);
    fclose(f);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Saved %d scenes", s_registry.count);
    
    return ESP_OK;
}

esp_err_t zigbee_scenes_load(void) {
    FILE *f = fopen("/spiffs/zigbee_scenes.json", "rb");
    if (!f) {
        ESP_LOGW(TAG, "No scenes file found");
        return ESP_ERR_NOT_FOUND;
    }
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json_str = malloc(len + 1);
    if (!json_str) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    fread(json_str, 1, len, f);
    json_str[len] = '\0';
    fclose(f);
    
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        return ESP_FAIL;
    }
    
    s_registry.count = 0;
    
    cJSON *scene_json = NULL;
    cJSON_ArrayForEach(scene_json, root) {
        if (s_registry.count >= ZIGBEE_MAX_SCENES) {
            break;
        }
        
        zigbee_scene_t *scene = &s_registry.scenes[s_registry.count];
        
        cJSON *id = cJSON_GetObjectItem(scene_json, "id");
        cJSON *name = cJSON_GetObjectItem(scene_json, "name");
        cJSON *enabled = cJSON_GetObjectItem(scene_json, "enabled");
        cJSON *devices = cJSON_GetObjectItem(scene_json, "devices");
        
        if (id && name && devices) {
            scene->scene_id = id->valueint;
            strncpy(scene->name, name->valuestring, ZIGBEE_SCENE_NAME_LEN - 1);
            scene->enabled = enabled ? enabled->valueint : true;
            
            scene->device_count = 0;
            cJSON *dev_json = NULL;
            cJSON_ArrayForEach(dev_json, devices) {
                if (scene->device_count < ZIGBEE_MAX_SCENE_DEVICES) {
                    zigbee_scene_device_t *dev = &scene->devices[scene->device_count++];
                    
                    cJSON *addr = cJSON_GetObjectItem(dev_json, "addr");
                    cJSON *on = cJSON_GetObjectItem(dev_json, "on");
                    cJSON *level = cJSON_GetObjectItem(dev_json, "level");
                    cJSON *color = cJSON_GetObjectItem(dev_json, "color");
                    cJSON *temp = cJSON_GetObjectItem(dev_json, "temp");
                    
                    dev->short_addr = addr->valueint;
                    dev->has_on_off = (on != NULL);
                    dev->on_off = on ? on->valueint : false;
                    dev->has_level = (level != NULL);
                    dev->level = level ? level->valueint : 0;
                    dev->has_color = (color != NULL);
                    dev->color_rgb = color ? color->valueint : 0;
                    dev->has_color_temp = (temp != NULL);
                    dev->color_temp = temp ? temp->valueint : 0;
                }
            }
            
            s_registry.count++;
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Loaded %d scenes", s_registry.count);
    return ESP_OK;
}
