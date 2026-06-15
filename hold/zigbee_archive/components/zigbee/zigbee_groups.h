/**
 * @file zigbee_groups.h
 * @brief Zigbee device group management
 * 
 * Allows controlling multiple devices as a single unit.
 * Example: "Living Room Lights" group controls 3 bulbs simultaneously.
 */

#ifndef ZIGBEE_GROUPS_H
#define ZIGBEE_GROUPS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ZIGBEE_MAX_GROUPS 16
#define ZIGBEE_MAX_GROUP_DEVICES 32
#define ZIGBEE_GROUP_NAME_LEN 32

/**
 * Device group structure
 */
typedef struct {
    uint8_t group_id;                                    // Group ID (0-15)
    char name[ZIGBEE_GROUP_NAME_LEN];                   // Friendly name
    uint16_t devices[ZIGBEE_MAX_GROUP_DEVICES];         // Device short addresses
    uint8_t device_count;                                // Number of devices in group
    bool enabled;                                        // Group enabled/disabled
} zigbee_group_t;

/**
 * @brief Initialize group management
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_init(void);

/**
 * @brief Deinitialize group management
 */
void zigbee_groups_deinit(void);

/**
 * @brief Create a new group
 * @param name Group name
 * @param group_id Output: assigned group ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_create(const char *name, uint8_t *group_id);

/**
 * @brief Delete a group
 * @param group_id Group ID
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_delete(uint8_t group_id);

/**
 * @brief Add device to group
 * @param group_id Group ID
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_add_device(uint8_t group_id, uint16_t short_addr);

/**
 * @brief Remove device from group
 * @param group_id Group ID
 * @param short_addr Device short address
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_remove_device(uint8_t group_id, uint16_t short_addr);

/**
 * @brief Control group (on/off)
 * @param group_id Group ID
 * @param on True for on, false for off
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_set_on_off(uint8_t group_id, bool on);

/**
 * @brief Control group brightness
 * @param group_id Group ID
 * @param level Brightness level (0-100%)
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_set_level(uint8_t group_id, uint8_t level);

/**
 * @brief Control group color
 * @param group_id Group ID
 * @param rgb RGB color (0xRRGGBB)
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_set_color(uint8_t group_id, uint32_t rgb);

/**
 * @brief Get group info
 * @param group_id Group ID
 * @return Pointer to group structure or NULL
 */
const zigbee_group_t* zigbee_groups_get(uint8_t group_id);

/**
 * @brief Get all groups
 * @param groups Output array
 * @param count Output: number of groups
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_get_all(zigbee_group_t *groups, int *count);

/**
 * @brief Save groups to storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_save(void);

/**
 * @brief Load groups from storage
 * @return ESP_OK on success
 */
esp_err_t zigbee_groups_load(void);

#endif // ZIGBEE_GROUPS_H
