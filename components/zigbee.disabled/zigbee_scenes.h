/**
 * @file zigbee_scenes.h
 * @brief Zigbee scene management
 * 
 * Save and recall device states.
 * Example: "Movie Mode" - dim lights to 20%, close blinds, set TV backlight to blue
 */

#ifndef ZIGBEE_SCENES_H
#define ZIGBEE_SCENES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ZIGBEE_MAX_SCENES 32
#define ZIGBEE_MAX_SCENE_DEVICES 16
#define ZIGBEE_SCENE_NAME_LEN 32

/**
 * Device state in scene
 */
typedef struct {
    uint16_t short_addr;        // Device short address
    bool has_on_off;            // Has on/off state
    bool on_off;                // On/off state
    bool has_level;             // Has brightness level
    uint8_t level;              // Brightness level (0-100%)
    bool has_color;             // Has color
    uint32_t color_rgb;         // RGB color
    bool has_color_temp;        // Has color temperature
    uint16_t color_temp;        // Color temperature (mireds)
} zigbee_scene_device_t;

/**
 * Scene structure
 */
typedef struct {
    uint8_t scene_id;                                   // Scene ID (0-31)
    char name[ZIGBEE_SCENE_NAME_LEN];                  // Friendly name
    zigbee_scene_device_t devices[ZIGBEE_MAX_SCENE_DEVICES]; // Device states
    uint8_t device_count;                               // Number of devices
    bool enabled;                                       // Scene enabled/disabled
} zigbee_scene_t;

/**
 * @brief Initialize scene management
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_init(void);

/**
 * @brief Deinitialize scene management
 */
void zigbee_scenes_deinit(void);

/**
 * @brief Create a new scene
 * @param name Scene name
 * @param scene_id Output: assigned scene ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_create(const char *name, uint8_t *scene_id);

/**
 * @brief Delete a scene
 * @param scene_id Scene ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_delete(uint8_t scene_id);

/**
 * @brief Capture current device state into scene
 * @param scene_id Scene ID
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_capture_device(uint8_t scene_id, uint16_t short_addr);

/**
 * @brief Remove device from scene
 * @param scene_id Scene ID
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_remove_device(uint8_t scene_id, uint16_t short_addr);

/**
 * @brief Recall scene (apply all device states)
 * @param scene_id Scene ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_recall(uint8_t scene_id);

/**
 * @brief Get scene info
 * @param scene_id Scene ID
 * @return Pointer to scene structure or NULL
 */
const zigbee_scene_t* zigbee_scenes_get(uint8_t scene_id);

/**
 * @brief Get all scenes
 * @param scenes Output array
 * @param count Output: number of scenes
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_get_all(zigbee_scene_t *scenes, int *count);

/**
 * @brief Save scenes to storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_save(void);

/**
 * @brief Load scenes from storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_scenes_load(void);

#endif // ZIGBEE_SCENES_H
