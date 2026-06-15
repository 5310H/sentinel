/**
 * @file zigbee_groups.c
 * @brief Zigbee device group management implementation
 */

#include "zigbee_groups.h"
#include "zigbee_mgr.h"
#include "storage_mgr.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ZIGBEE_GROUPS";

// Group registry
static struct {
    zigbee_group_t groups[ZIGBEE_MAX_GROUPS];
    int count;
    bool initialized;
} s_registry = {0};

esp_err_t zigbee_groups_init(void) {
    if (s_registry.initialized) {
        return ESP_OK;
    }
    
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.initialized = true;
    
    // Try to load from storage
    if (zigbee_groups_load() != ESP_OK) {
        ESP_LOGW(TAG, "No saved groups found, starting fresh");
    }
    
    ESP_LOGI(TAG, "Initialized with %d groups", s_registry.count);
    return ESP_OK;
}

void zigbee_groups_deinit(void) {
    if (!s_registry.initialized) {
        return;
    }
    
    zigbee_groups_save();
    memset(&s_registry, 0, sizeof(s_registry));
}

esp_err_t zigbee_groups_create(const char *name, uint8_t *group_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!name || !group_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_registry.count >= ZIGBEE_MAX_GROUPS) {
        ESP_LOGE(TAG, "Maximum groups reached (%d)", ZIGBEE_MAX_GROUPS);
        return ESP_ERR_NO_MEM;
    }
    
    // Find free group ID
    uint8_t new_id = 0;
    for (int i = 0; i < ZIGBEE_MAX_GROUPS; i++) {
        bool id_used = false;
        for (int j = 0; j < s_registry.count; j++) {
            if (s_registry.groups[j].group_id == i) {
                id_used = true;
                break;
            }
        }
        if (!id_used) {
            new_id = i;
            break;
        }
    }
    
    // Initialize group
    zigbee_group_t *group = &s_registry.groups[s_registry.count];
    group->group_id = new_id;
    strncpy(group->name, name, ZIGBEE_GROUP_NAME_LEN - 1);
    group->name[ZIGBEE_GROUP_NAME_LEN - 1] = '\0';
    group->device_count = 0;
    group->enabled = true;
    
    s_registry.count++;
    *group_id = new_id;
    
    ESP_LOGI(TAG, "Created group %d: %s", new_id, name);
    zigbee_groups_save();
    
    return ESP_OK;
}

esp_err_t zigbee_groups_delete(uint8_t group_id) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Find group
    int idx = -1;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.groups[i].group_id == group_id) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining groups
    for (int i = idx; i < s_registry.count - 1; i++) {
        s_registry.groups[i] = s_registry.groups[i + 1];
    }
    s_registry.count--;
    
    ESP_LOGI(TAG, "Deleted group %d", group_id);
    zigbee_groups_save();
    
    return ESP_OK;
}

esp_err_t zigbee_groups_add_device(uint8_t group_id, uint16_t short_addr) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Find group
    zigbee_group_t *group = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.groups[i].group_id == group_id) {
            group = &s_registry.groups[i];
            break;
        }
    }
    
    if (!group) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if device already in group
    for (int i = 0; i < group->device_count; i++) {
        if (group->devices[i] == short_addr) {
            return ESP_OK; // Already added
        }
    }
    
    if (group->device_count >= ZIGBEE_MAX_GROUP_DEVICES) {
        ESP_LOGE(TAG, "Group %d is full", group_id);
        return ESP_ERR_NO_MEM;
    }
    
    group->devices[group->device_count++] = short_addr;
    
    ESP_LOGI(TAG, "Added device 0x%04X to group %d", short_addr, group_id);
    zigbee_groups_save();
    
    return ESP_OK;
}

esp_err_t zigbee_groups_remove_device(uint8_t group_id, uint16_t short_addr) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Find group
    zigbee_group_t *group = NULL;
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.groups[i].group_id == group_id) {
            group = &s_registry.groups[i];
            break;
        }
    }
    
    if (!group) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Find and remove device
    int idx = -1;
    for (int i = 0; i < group->device_count; i++) {
        if (group->devices[i] == short_addr) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining devices
    for (int i = idx; i < group->device_count - 1; i++) {
        group->devices[i] = group->devices[i + 1];
    }
    group->device_count--;
    
    ESP_LOGI(TAG, "Removed device 0x%04X from group %d", short_addr, group_id);
    zigbee_groups_save();
    
    return ESP_OK;
}

esp_err_t zigbee_groups_set_on_off(uint8_t group_id, bool on) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const zigbee_group_t *group = zigbee_groups_get(group_id);
    if (!group || !group->enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Setting group %d to %s", group_id, on ? "ON" : "OFF");
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < group->device_count; i++) {
        if (zigbee_mgr_set_on_off(group->devices[i], on) == ESP_OK) {
            success++;
        } else {
            failed++;
        }
    }
    
    ESP_LOGI(TAG, "Group %d control: %d success, %d failed", group_id, success, failed);
    
    return (success > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t zigbee_groups_set_level(uint8_t group_id, uint8_t level) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const zigbee_group_t *group = zigbee_groups_get(group_id);
    if (!group || !group->enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Setting group %d level to %d%%", group_id, level);
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < group->device_count; i++) {
        if (zigbee_mgr_set_level(group->devices[i], level) == ESP_OK) {
            success++;
        } else {
            failed++;
        }
    }
    
    ESP_LOGI(TAG, "Group %d control: %d success, %d failed", group_id, success, failed);
    
    return (success > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t zigbee_groups_set_color(uint8_t group_id, uint32_t rgb) {
    if (!s_registry.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const zigbee_group_t *group = zigbee_groups_get(group_id);
    if (!group || !group->enabled) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Setting group %d color to 0x%06X", group_id, rgb);
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < group->device_count; i++) {
        if (zigbee_mgr_set_color(group->devices[i], rgb) == ESP_OK) {
            success++;
        } else {
            failed++;
        }
    }
    
    ESP_LOGI(TAG, "Group %d control: %d success, %d failed", group_id, success, failed);
    
    return (success > 0) ? ESP_OK : ESP_FAIL;
}

const zigbee_group_t* zigbee_groups_get(uint8_t group_id) {
    if (!s_registry.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        if (s_registry.groups[i].group_id == group_id) {
            return &s_registry.groups[i];
        }
    }
    
    return NULL;
}

esp_err_t zigbee_groups_get_all(zigbee_group_t *groups, int *count) {
    if (!s_registry.initialized || !groups || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(groups, s_registry.groups, sizeof(zigbee_group_t) * s_registry.count);
    *count = s_registry.count;
    
    return ESP_OK;
}

esp_err_t zigbee_groups_save(void) {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < s_registry.count; i++) {
        zigbee_group_t *group = &s_registry.groups[i];
        
        cJSON *g = cJSON_CreateObject();
        cJSON_AddNumberToObject(g, "id", group->group_id);
        cJSON_AddStringToObject(g, "name", group->name);
        cJSON_AddBoolToObject(g, "enabled", group->enabled);
        
        cJSON *devices = cJSON_CreateArray();
        for (int j = 0; j < group->device_count; j++) {
            cJSON_AddItemToArray(devices, cJSON_CreateNumber(group->devices[j]));
        }
        cJSON_AddItemToObject(g, "devices", devices);
        
        cJSON_AddItemToArray(root, g);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    FILE *f = fopen("/spiffs/zigbee_groups.json", "w");
    if (!f) {
        cJSON_free(json_str);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    fputs(json_str, f);
    fclose(f);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Saved %d groups", s_registry.count);
    
    return ESP_OK;
}

esp_err_t zigbee_groups_load(void) {
    FILE *f = fopen("/spiffs/zigbee_groups.json", "rb");
    if (!f) {
        ESP_LOGW(TAG, "No groups file found");
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
    
    cJSON *group_json = NULL;
    cJSON_ArrayForEach(group_json, root) {
        if (s_registry.count >= ZIGBEE_MAX_GROUPS) {
            break;
        }
        
        zigbee_group_t *group = &s_registry.groups[s_registry.count];
        
        cJSON *id = cJSON_GetObjectItem(group_json, "id");
        cJSON *name = cJSON_GetObjectItem(group_json, "name");
        cJSON *enabled = cJSON_GetObjectItem(group_json, "enabled");
        cJSON *devices = cJSON_GetObjectItem(group_json, "devices");
        
        if (id && name && devices) {
            group->group_id = id->valueint;
            strncpy(group->name, name->valuestring, ZIGBEE_GROUP_NAME_LEN - 1);
            group->enabled = enabled ? enabled->valueint : true;
            
            group->device_count = 0;
            cJSON *device = NULL;
            cJSON_ArrayForEach(device, devices) {
                if (group->device_count < ZIGBEE_MAX_GROUP_DEVICES) {
                    group->devices[group->device_count++] = device->valueint;
                }
            }
            
            s_registry.count++;
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Loaded %d groups", s_registry.count);
    return ESP_OK;
}
