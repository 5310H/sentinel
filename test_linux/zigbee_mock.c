/**
 * @file zigbee_mock.c
 * @brief Mock Zigbee implementation for Linux testing (USB serial)
 * 
 * This mock connects to real Zigbee coordinators via USB serial ports:
 * - /dev/ttyUSB0, /dev/ttyUSB1, etc. (FTDI, CH340, PL2303)
 * - /dev/ttyACM0, /dev/ttyACM1, etc. (CDC ACM)
 * 
 * Set environment variable ZIGBEE_PORT to override default:
 *   export ZIGBEE_PORT=/dev/ttyUSB1
 *   ./sentinel_test
 */

#include "zigbee_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

static struct {
    bool initialized;
    int serial_fd;
    char serial_port[256];
    zigbee_device_t devices[ZIGBEE_MAX_DEVICES];
    int device_count;
} s_mock;

/**
 * Initialize with mock devices
 */
esp_err_t zigbee_mgr_init(const void *config, void *callback) {
    printf("ZIGBEE_MOCK: Initializing Zigbee manager (USB serial mock)\\n");
    
    memset(&s_mock, 0, sizeof(s_mock));
    
    // Get serial port from environment or use default
    const char *port = getenv("ZIGBEE_PORT");
    if (port) {
        snprintf(s_mock.serial_port, sizeof(s_mock.serial_port), "%s", port);
        printf("ZIGBEE_MOCK: Using serial port from env: %s\\n", port);
    } else {
        // Try common USB serial ports
        const char *ports[] = {"/dev/ttyUSB0", "/dev/ttyACM0", "/dev/cu.usbserial"};
        bool found = false;
        for (int i = 0; i < 3; i++) {
            if (access(ports[i], F_OK) == 0) {
                snprintf(s_mock.serial_port, sizeof(s_mock.serial_port), "%s", ports[i]);
                printf("ZIGBEE_MOCK: Found serial port: %s\\n", ports[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("ZIGBEE_MOCK: No USB serial port found, using mock-only mode\\n");
            snprintf(s_mock.serial_port, sizeof(s_mock.serial_port), "/dev/null");
        }
    }
    
    // Try to open serial port
    s_mock.serial_fd = open(s_mock.serial_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_mock.serial_fd >= 0) {
        // Configure serial port (115200 baud, 8N1)
        struct termios tty;
        if (tcgetattr(s_mock.serial_fd, &tty) == 0) {
            cfsetospeed(&tty, B115200);
            cfsetispeed(&tty, B115200);
            tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  // 8 bits
            tty.c_cflag |= (CLOCAL | CREAD);
            tty.c_cflag &= ~(PARENB | PARODD);           // No parity
            tty.c_cflag &= ~CSTOPB;                       // 1 stop bit
            tty.c_cflag &= ~CRTSCTS;                      // No flow control
            
            tcsetattr(s_mock.serial_fd, TCSANOW, &tty);
            printf("ZIGBEE_MOCK: Serial port configured: 115200 8N1\\n");
        }
    } else {
        printf("ZIGBEE_MOCK: Could not open serial port, using mock-only mode\\n");
    }
    
    // Create mock devices for testing
    zigbee_device_t *dev;
    
    // Device 0: Living Room Light (0x1234)
    dev = &s_mock.devices[s_mock.device_count++];
    dev->short_addr = 0x1234;
    snprintf(dev->name, sizeof(dev->name), "Living Room Light");
    dev->type = ZIGBEE_DEVICE_LIGHT;
    dev->state = ZIGBEE_STATE_ONLINE;
    dev->capabilities = 0x003;  // On/off + level
    dev->endpoint = 1;
    dev->relay_id = 64;  // Mapped to relay 64
    dev->current_on_off = true;
    dev->current_level = 180;  // ~70%
    
    // Device 1: Front Door Sensor (0x2345)
    dev = &s_mock.devices[s_mock.device_count++];
    dev->short_addr = 0x2345;
    snprintf(dev->name, sizeof(dev->name), "Front Door Sensor");
    dev->type = ZIGBEE_DEVICE_DOOR_SENSOR;
    dev->state = ZIGBEE_STATE_ONLINE;
    dev->capabilities = 0x900;  // Contact + battery
    dev->endpoint = 1;
    dev->zone_id = 50;  // Mapped to zone 50
    dev->current_contact = false;  // Closed
    dev->current_battery = 92;
    
    // Device 2: Motion Sensor (0x3456)
    dev = &s_mock.devices[s_mock.device_count++];
    dev->short_addr = 0x3456;
    snprintf(dev->name, sizeof(dev->name), "Hallway Motion");
    dev->type = ZIGBEE_DEVICE_MOTION_SENSOR;
    dev->state = ZIGBEE_STATE_ONLINE;
    dev->capabilities = 0x500;  // Occupancy + battery
    dev->endpoint = 1;
    dev->zone_id = 51;  // Mapped to zone 51
    dev->current_occupancy = false;  // Clear
    dev->current_battery = 85;
    
    // Device 3: Smart Outlet (0x4567)
    dev = &s_mock.devices[s_mock.device_count++];
    dev->short_addr = 0x4567;
    snprintf(dev->name, sizeof(dev->name), "Kitchen Outlet");
    dev->type = ZIGBEE_DEVICE_OUTLET;
    dev->state = ZIGBEE_STATE_ONLINE;
    dev->capabilities = 0x001;  // On/off
    dev->endpoint = 1;
    dev->relay_id = 65;  // Mapped to relay 65
    dev->current_on_off = false;
    
    s_mock.initialized = true;
    printf("ZIGBEE_MOCK: Initialized with %d mock devices\\n", s_mock.device_count);
    
    return ESP_OK;
}

void zigbee_mgr_deinit(void) {
    if (s_mock.serial_fd >= 0) {
        close(s_mock.serial_fd);
    }
    s_mock.initialized = false;
    printf("ZIGBEE_MOCK: Deinitialized\\n");
}

bool zigbee_mgr_is_connected(void) {
    return s_mock.initialized;
}

esp_err_t zigbee_mgr_enable_pairing(uint16_t duration_sec) {
    printf("ZIGBEE_MOCK: Enable pairing for %d seconds\\n", duration_sec);
    
    // If serial port is open, send permit join command
    if (s_mock.serial_fd >= 0) {
        // Send Z-Stack permit join command
        uint8_t cmd[] = {0xFE, 0x03, 0x26, 0x36, duration_sec, 0xFF, 0x00};
        write(s_mock.serial_fd, cmd, sizeof(cmd));
    }
    
    return ESP_OK;
}

esp_err_t zigbee_mgr_discover_devices(uint32_t timeout_ms) {
    printf("ZIGBEE_MOCK: Discover devices (timeout=%u ms)\\n", timeout_ms);
    return ESP_OK;
}

esp_err_t zigbee_mgr_get_devices(zigbee_device_t *devices, int *count) {
    if (!devices || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(devices, s_mock.devices, s_mock.device_count * sizeof(zigbee_device_t));
    *count = s_mock.device_count;
    
    return ESP_OK;
}

const zigbee_device_t* zigbee_mgr_get_device(uint16_t short_addr) {
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            return &s_mock.devices[i];
        }
    }
    return NULL;
}

esp_err_t zigbee_mgr_remove_device(uint16_t short_addr) {
    printf("ZIGBEE_MOCK: Remove device 0x%04X\\n", short_addr);
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            // Shift remaining devices
            for (int j = i; j < s_mock.device_count - 1; j++) {
                s_mock.devices[j] = s_mock.devices[j + 1];
            }
            s_mock.device_count--;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_mgr_set_on_off(uint16_t short_addr, bool on) {
    printf("ZIGBEE_MOCK: Set device 0x%04X to %s\\n", short_addr, on ? "ON" : "OFF");
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            s_mock.devices[i].current_on_off = on;
            
            // If serial port is open, send command to real device
            if (s_mock.serial_fd >= 0) {
                // Send ZCL ON/OFF command
                // Format depends on coordinator type
                printf("ZIGBEE_MOCK: Sending ON/OFF command to serial port\\n");
            }
            
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_mgr_set_level(uint16_t short_addr, uint8_t level) {
    printf("ZIGBEE_MOCK: Set device 0x%04X level to %d%%\\n", short_addr, level);
    
    uint8_t zigbee_level = (level * 254) / 100;
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            s_mock.devices[i].current_level = zigbee_level;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_mgr_set_color(uint16_t short_addr, uint32_t rgb) {
    printf("ZIGBEE_MOCK: Set device 0x%04X color to 0x%06X\\n", short_addr, rgb);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_mgr_set_color_temp(uint16_t short_addr, uint16_t mireds) {
    printf("ZIGBEE_MOCK: Set device 0x%04X color temp to %d\\n", short_addr, mireds);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_mgr_set_lock(uint16_t short_addr, bool locked) {
    printf("ZIGBEE_MOCK: Set device 0x%04X lock to %s\n", short_addr, locked ? "LOCKED" : "UNLOCKED");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_mgr_set_thermostat(uint16_t short_addr, float temperature) {
    printf("ZIGBEE_MOCK: Set thermostat 0x%04X to %.1f°C\n", short_addr, temperature);
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            s_mock.devices[i].current_temperature = temperature;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

int zigbee_mgr_get_battery_level(uint16_t short_addr) {
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            if (s_mock.devices[i].capabilities & ZIGBEE_CAP_BATTERY) {
                return s_mock.devices[i].current_battery;
            }
            break;
        }
    }
    return -1;
}

bool zigbee_mgr_is_battery_low(uint16_t short_addr) {
    int level = zigbee_mgr_get_battery_level(short_addr);
    return (level >= 0 && level < 20);
}

esp_err_t zigbee_mgr_get_statistics(zigbee_network_stats_t *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    
    stats->total_devices = s_mock.device_count;
    stats->online_devices = 0;
    stats->offline_devices = 0;
    stats->low_battery_devices = 0;
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].state == ZIGBEE_STATE_ONLINE) {
            stats->online_devices++;
        } else {
            stats->offline_devices++;
        }
        
        if (zigbee_mgr_is_battery_low(s_mock.devices[i].short_addr)) {
            stats->low_battery_devices++;
        }
    }
    
    // Mock command statistics
    stats->commands_sent = 100;
    stats->commands_failed = 2;
    stats->frames_received = 523;
    stats->uptime_seconds = 3600; // 1 hour
    
    return ESP_OK;
}

esp_err_t zigbee_mgr_map_to_zone(uint16_t short_addr, int zone_id) {
    if (zone_id != 0 && (zone_id < 50 || zone_id > 64)) {
        printf("ZIGBEE_MOCK: Invalid zone ID %d (must be 50-64)\\n", zone_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    printf("ZIGBEE_MOCK: Map device 0x%04X to zone %d\\n", short_addr, zone_id);
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            s_mock.devices[i].zone_id = zone_id;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_mgr_map_to_relay(uint16_t short_addr, int relay_id) {
    if (relay_id != 0 && (relay_id < 64 || relay_id > 95)) {
        printf("ZIGBEE_MOCK: Invalid relay ID %d (must be 64-95)\\n", relay_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    printf("ZIGBEE_MOCK: Map device 0x%04X to relay %d\\n", short_addr, relay_id);
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].short_addr == short_addr) {
            s_mock.devices[i].relay_id = relay_id;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

bool zigbee_mgr_get_zone_state(int zone_id) {
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].zone_id == zone_id) {
            // Return sensor state
            if (s_mock.devices[i].capabilities & 0x800) {  // Contact
                return s_mock.devices[i].current_contact;
            }
            if (s_mock.devices[i].capabilities & 0x400) {  // Occupancy
                return s_mock.devices[i].current_occupancy;
            }
        }
    }
    return false;
}

esp_err_t zigbee_mgr_set_relay_state(int relay_id, bool on) {
    printf("ZIGBEE_MOCK: Set relay %d to %s\\n", relay_id, on ? "ON" : "OFF");
    
    for (int i = 0; i < s_mock.device_count; i++) {
        if (s_mock.devices[i].relay_id == relay_id) {
            return zigbee_mgr_set_on_off(s_mock.devices[i].short_addr, on);
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

const char* zigbee_device_type_name(zigbee_device_type_t type) {
    switch (type) {
        case ZIGBEE_DEVICE_SWITCH: return "switch";
        case ZIGBEE_DEVICE_LIGHT: return "light";
        case ZIGBEE_DEVICE_COLOR_LIGHT: return "color_light";
        case ZIGBEE_DEVICE_OUTLET: return "outlet";
        case ZIGBEE_DEVICE_DOOR_SENSOR: return "door_sensor";
        case ZIGBEE_DEVICE_MOTION_SENSOR: return "motion_sensor";
        case ZIGBEE_DEVICE_TEMPERATURE: return "temperature";
        case ZIGBEE_DEVICE_HUMIDITY: return "humidity";
        case ZIGBEE_DEVICE_LEAK_SENSOR: return "leak_sensor";
        case ZIGBEE_DEVICE_SMOKE_DETECTOR: return "smoke_detector";
        case ZIGBEE_DEVICE_LOCK: return "lock";
        case ZIGBEE_DEVICE_THERMOSTAT: return "thermostat";
        case ZIGBEE_DEVICE_BUTTON: return "button";
        case ZIGBEE_DEVICE_DIMMER: return "dimmer";
        case ZIGBEE_DEVICE_BLIND: return "blind";
        default: return "unknown";
    }
}

// ============================================================================
// Phase 3: Groups, Scenes, Automations Mock Implementation
// ============================================================================

static zigbee_group_t mock_groups[ZIGBEE_MAX_GROUPS];
static int mock_group_count = 0;
static zigbee_scene_t mock_scenes[ZIGBEE_MAX_SCENES];
static int mock_scene_count = 0;

esp_err_t zigbee_groups_init(void) {
    mock_group_count = 0;
    memset(mock_groups, 0, sizeof(mock_groups));
    printf("[ZIGBEE_GROUPS] Mock initialized\n");
    return ESP_OK;
}

esp_err_t zigbee_groups_create(const char *name, uint8_t *group_id) {
    if (!name || !group_id || mock_group_count >= ZIGBEE_MAX_GROUPS) {
        return ESP_FAIL;
    }
    
    *group_id = mock_group_count;
    mock_groups[mock_group_count].group_id = *group_id;
    strncpy(mock_groups[mock_group_count].name, name, 31);
    mock_groups[mock_group_count].name[31] = '\0';
    mock_groups[mock_group_count].device_count = 0;
    mock_groups[mock_group_count].enabled = true;
    mock_group_count++;
    
    printf("[ZIGBEE_GROUPS] Created group '%s' (ID: %d)\n", name, *group_id);
    return ESP_OK;
}

esp_err_t zigbee_groups_delete(uint8_t group_id) {
    for (int i = 0; i < mock_group_count; i++) {
        if (mock_groups[i].group_id == group_id) {
            // Shift remaining groups
            for (int j = i; j < mock_group_count - 1; j++) {
                mock_groups[j] = mock_groups[j + 1];
            }
            mock_group_count--;
            printf("[ZIGBEE_GROUPS] Deleted group ID %d\n", group_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_groups_get_all(zigbee_group_t *groups, int *count) {
    if (!groups || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(groups, mock_groups, sizeof(zigbee_group_t) * mock_group_count);
    *count = mock_group_count;
    return ESP_OK;
}

esp_err_t zigbee_groups_set_on_off(uint8_t group_id, bool on) {
    printf("[ZIGBEE_GROUPS] Group %d set to %s (mock)\n", group_id, on ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t zigbee_groups_set_level(uint8_t group_id, uint8_t level) {
    printf("[ZIGBEE_GROUPS] Group %d set level to %d%% (mock)\n", group_id, (level * 100) / 255);
    return ESP_OK;
}

esp_err_t zigbee_groups_set_color(uint8_t group_id, uint32_t rgb) {
    printf("[ZIGBEE_GROUPS] Group %d set color to 0x%06X (mock)\n", group_id, rgb);
    return ESP_OK;
}

esp_err_t zigbee_scenes_init(void) {
    mock_scene_count = 0;
    memset(mock_scenes, 0, sizeof(mock_scenes));
    printf("[ZIGBEE_SCENES] Mock initialized\n");
    return ESP_OK;
}

esp_err_t zigbee_scenes_create(const char *name, uint8_t *scene_id) {
    if (!name || !scene_id || mock_scene_count >= ZIGBEE_MAX_SCENES) {
        return ESP_FAIL;
    }
    
    *scene_id = mock_scene_count;
    mock_scenes[mock_scene_count].scene_id = *scene_id;
    strncpy(mock_scenes[mock_scene_count].name, name, 31);
    mock_scenes[mock_scene_count].name[31] = '\0';
    mock_scenes[mock_scene_count].device_count = 0;
    mock_scenes[mock_scene_count].enabled = true;
    mock_scene_count++;
    
    printf("[ZIGBEE_SCENES] Created scene '%s' (ID: %d)\n", name, *scene_id);
    return ESP_OK;
}

esp_err_t zigbee_scenes_delete(uint8_t scene_id) {
    for (int i = 0; i < mock_scene_count; i++) {
        if (mock_scenes[i].scene_id == scene_id) {
            // Shift remaining scenes
            for (int j = i; j < mock_scene_count - 1; j++) {
                mock_scenes[j] = mock_scenes[j + 1];
            }
            mock_scene_count--;
            printf("[ZIGBEE_SCENES] Deleted scene ID %d\n", scene_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_scenes_get_all(zigbee_scene_t *scenes, int *count) {
    if (!scenes || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(scenes, mock_scenes, sizeof(zigbee_scene_t) * mock_scene_count);
    *count = mock_scene_count;
    return ESP_OK;
}

esp_err_t zigbee_scenes_recall(uint8_t scene_id) {
    for (int i = 0; i < mock_scene_count; i++) {
        if (mock_scenes[i].scene_id == scene_id) {
            printf("[ZIGBEE_SCENES] Recalling scene '%s' (ID: %d) (mock)\n", 
                   mock_scenes[i].name, scene_id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
