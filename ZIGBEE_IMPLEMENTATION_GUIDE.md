# Zigbee Device Integration Guide

## Overview

The Sentinel system integrates with Zigbee devices through an external Zigbee coordinator connected via UART. This enables control and monitoring of switches, lights, sensors, locks, and other Zigbee smart home devices directly from the alarm system.

### Key Features
- **External Coordinator**: Supports TI CC2652P, CC2652RB, Silicon Labs EFR32, and generic coordinators
- **Device Types**: 16 types including switches, lights, sensors, locks, thermostats
- **Virtual Integration**: Sensors map to zones 50-64, outputs map to relays 64-95
- **Real-time Updates**: Background UART receiver for device state synchronization
- **Web Management**: Responsive UI for pairing, control, and zone/relay mapping
- **REST API**: 7 endpoints for complete device management
- **USB Mock**: Linux testing with real USB coordinators or mock devices
- **Advanced Automation**: Groups, scenes, and scheduler-based automations (Phase 3)

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Sentinel Engine                      │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐       │
│  │ Zone 50-64 │  │Relay 64-95 │  │  Web UI    │       │
│  └─────┬──────┘  └──────┬─────┘  └──────┬─────┘       │
│        │                │                │              │
└────────┼────────────────┼────────────────┼──────────────┘
         │                │                │
    ┌────▼────────────────▼────────────────▼─────┐
    │       Zigbee Manager (zigbee_mgr)          │
    │  - Device registry (64 devices max)        │
    │  - Heartbeat timer (30s)                   │
    │  - Background RX task                      │
    │  - Frame callbacks                         │
    └────────────────┬───────────────────────────┘
                     │
    ┌────────────────▼───────────────────────────┐
    │    Zigbee Protocol (zigbee_protocol)       │
    │  - UART communication (115200 baud)        │
    │  - Frame encoding/decoding                 │
    │  - Coordinator abstraction                 │
    └────────────────┬───────────────────────────┘
                     │
              ┌──────▼──────┐
              │  GPIO18/19  │  Zigbee Coordinator
              │   (TX/RX)   │  (CC2652P/EFR32)
              └──────────────┘
```

## Hardware Setup

### Supported Zigbee Coordinators

#### TI CC2652P USB Stick
- **Chipset**: TI CC2652P (Zigbee 3.0)
- **Firmware**: Z-Stack 3.x
- **Connection**: USB or UART (3.3V)
- **Range**: ~100m line-of-sight
- **Recommended**: Excellent compatibility

#### TI CC2652RB USB Stick
- **Chipset**: TI CC2652RB
- **Firmware**: Z-Stack 3.x
- **Connection**: USB or UART (3.3V)
- **Range**: ~70m line-of-sight
- **Note**: Slightly less range than CC2652P

#### Silicon Labs EFR32 Series
- **Chipset**: EFR32MG12/13/21
- **Firmware**: EmberZNet 6.x/7.x
- **Connection**: UART (3.3V)
- **Range**: ~100m line-of-sight
- **Note**: Different command set

#### Generic Zigbee Coordinators
- Conbee II, ZZH!, Sonoff ZBBridge (flashed)
- Most Zigbee 3.0 coordinators with serial interface

### KC868-A8 Wiring (ESP32)

Connect coordinator to UART1 on KC868-A8:

```
┌─────────────────┐
│    ESP32-S3     │
│   (KC868-A8)    │
│                 │
│  GPIO18 (TX) ───┼───> RX   ┐
│                 │           │  Zigbee
│  GPIO19 (RX) <──┼────  TX  │  Coordinator
│                 │           │  (CC2652P)
│  3.3V ──────────┼──── VCC  │
│                 │           │
│  GND ───────────┼──── GND  │
└─────────────────┘           ┘
```

**Pin Configuration:**
- **TX**: GPIO18 (ESP32 → Coordinator)
- **RX**: GPIO19 (Coordinator → ESP32)
- **Baud Rate**: 115200
- **Voltage**: 3.3V (DO NOT USE 5V)

### Linux Mock Setup (USB Serial)

For testing on Linux with a real USB coordinator:

```bash
# The mock automatically detects USB serial ports:
# - /dev/ttyUSB0 (FTDI, CH340 adapters)
# - /dev/ttyACM0 (CDC ACM devices)
# - /dev/cu.usbserial* (macOS)

# Override port with environment variable:
export ZIGBEE_PORT=/dev/ttyUSB0
./sentinel_test
```

## Device Types

### Output Devices (Control)
| Type | Capabilities | Description |
|------|-------------|-------------|
| **SWITCH** | ON_OFF | Basic on/off switch |
| **LIGHT** | ON_OFF, LEVEL | Dimmable light (0-254) |
| **COLOR_LIGHT** | ON_OFF, LEVEL, COLOR | RGB LED with brightness |
| **OUTLET** | ON_OFF | Smart plug |
| **BLIND** | LEVEL | Window shade (0-100%) |
| **LOCK** | LOCK | Smart door lock |
| **THERMOSTAT** | TEMPERATURE | HVAC control |
| **DIMMER** | LEVEL | Standalone dimmer |

### Sensor Devices (Monitoring)
| Type | Capabilities | States |
|------|-------------|--------|
| **DOOR_SENSOR** | CONTACT, BATTERY | Open/Closed, Battery % |
| **MOTION_SENSOR** | OCCUPANCY, BATTERY | Motion/Clear, Battery % |
| **TEMPERATURE** | TEMPERATURE | Temperature (°C) |
| **HUMIDITY** | HUMIDITY | Humidity (%RH) |
| **LEAK_SENSOR** | LEAK, BATTERY | Dry/Wet, Battery % |
| **SMOKE_DETECTOR** | SMOKE, BATTERY | Normal/Alarm, Battery % |
| **BUTTON** | - | Wireless switch |

## Virtual Zone & Relay Allocation

### System-Wide Allocation
```
ZONES:
├─ 1-8:    Physical zones (GPIO/I2C)
├─ 9-32:   Reserved
├─ 33-49:  Matter devices (17 zones)
├─ 50-64:  Zigbee devices (15 zones) ◄── THIS
└─ 65-96:  Tuya devices (32 zones)

RELAYS:
├─ 1-7:    Physical relays (GPIO)
├─ 8-31:   Matter devices (24 relays)
├─ 32-63:  Tuya devices (32 relays)
└─ 64-95:  Zigbee devices (32 relays) ◄── THIS
```

### Mapping Examples

#### Sensor to Zone
```bash
# Map front door sensor (0x2345) to zone 50
curl -X POST http://192.168.1.100/api/zigbee/device/0x2345/map_zone \
  -H "Content-Type: application/json" \
  -d '{"zone_id": 50}'
```

#### Output to Relay
```bash
# Map living room light (0x1234) to relay 64
curl -X POST http://192.168.1.100/api/zigbee/device/0x1234/map_relay \
  -H "Content-Type: application/json" \
  -d '{"relay_id": 64}'
```

## REST API Reference

### 1. GET /api/zigbee/devices
List all Zigbee devices with current status.

**Response:**
```json
{
  "connected": true,
  "count": 4,
  "devices": [
    {
      "short_addr": "0x1234",
      "name": "Living Room Light",
      "type": "light",
      "online": true,
      "current_on_off": true,
      "current_level": 180,
      "relay_id": 64
    },
    {
      "short_addr": "0x2345",
      "name": "Front Door Sensor",
      "type": "door_sensor",
      "online": true,
      "current_contact": false,
      "current_battery": 92,
      "zone_id": 50
    }
  ]
}
```

### 2. POST /api/zigbee/discover
Trigger device discovery (searches for new devices on the network).

**Request:**
```json
{
  "timeout_ms": 10000
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Discovery started"
}
```

### 3. POST /api/zigbee/pair
Enable pairing mode (allows new devices to join the network).

**Request:**
```json
{
  "duration_sec": 120
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Pairing mode enabled"
}
```

### 4. DELETE /api/zigbee/device/:addr
Remove a device from the network.

**Example:**
```bash
curl -X DELETE http://192.168.1.100/api/zigbee/device/0x1234
```

**Response:**
```json
{
  "status": "ok"
}
```

### 5. POST /api/zigbee/device/:addr/control
Control a device (switch, brightness, lock).

**Switch Control:**
```json
{
  "action": "on_off",
  "value": true
}
```

**Brightness Control:**
```json
{
  "action": "level",
  "value": 75
}
```

**Lock Control:**
```json
{
  "action": "lock",
  "value": true
}
```

**Response:**
```json
{
  "status": "ok"
}
```

### 6. POST /api/zigbee/device/:addr/map_zone
Map a sensor device to a virtual zone (50-64).

**Request:**
```json
{
  "zone_id": 50
}
```

**Response:**
```json
{
  "status": "ok"
}
```

### 7. POST /api/zigbee/device/:addr/map_relay
Map an output device to a virtual relay (64-95).

**Request:**
```json
{
  "relay_id": 64
}
```

**Response:**
```json
{
  "status": "ok"
}
```

## Web UI Usage

### Access
Navigate to: `http://192.168.1.100/zigbee.html`

### Features

#### Device Grid
- Real-time status with 5-second auto-refresh
- Color-coded online/offline indicators
- Device type badges (light, sensor, switch, etc.)
- Short address display (0xXXXX format)

#### Controls
- **Switches/Lights**: Toggle ON/OFF with animated buttons
- **Dimmable Lights**: Brightness slider (0-100%)
- **Locks**: Lock/Unlock button
- **Sensors**: Read-only state display

#### Pairing New Devices
1. Click "Pair Device" button
2. Put your Zigbee device into pairing mode (usually hold reset button)
3. Wait for device to appear in the grid (up to 60 seconds)
4. Assign name, zone, or relay as needed

#### Discovery
- Click "Discover" to scan for devices already on the network
- Useful for rediscovering devices after reboot
- Does not put devices into pairing mode

#### Zone/Relay Mapping
1. Click device name to open details
2. Click "Map to Zone" or "Map to Relay"
3. Enter zone ID (50-64) or relay ID (64-95)
4. Click "Save"

## Code Examples

### Initialization

```c
#include "zigbee_mgr.h"

void app_main(void) {
    // Initialize Zigbee manager
    if (zigbee_mgr_init(NULL, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Zigbee manager initialized");
    }
}
```

### Device Control

```c
// Turn on light at 75% brightness
uint16_t short_addr = 0x1234;
zigbee_mgr_set_on_off(short_addr, true);
zigbee_mgr_set_level(short_addr, 75);

// Lock a door
zigbee_mgr_set_lock(0x5678, true);
```

### Zone Integration

```c
#include "zigbee_zones.h"

// Read zone state (for engine)
bool state = zigbee_zone_get_state(50);  // Zone 50
if (state) {
    ESP_LOGI(TAG, "Zone 50 triggered (door open or motion)");
}
```

### Relay Integration

```c
#include "zigbee_relays.h"

// Control relay (from engine automation)
zigbee_relay_set_state(64, true);   // Turn on relay 64
zigbee_relay_set_state(65, false);  // Turn off relay 65
```

### Get All Devices

```c
zigbee_device_t devices[64];
int count = 0;
zigbee_mgr_get_devices(devices, &count);

for (int i = 0; i < count; i++) {
    printf("Device 0x%04x: %s (%s)\n",
        devices[i].short_addr,
        devices[i].name,
        zigbee_device_type_name(devices[i].type));
}
```

## Component Structure

### Files

```
components/zigbee/
├── zigbee_protocol.h/c     # UART communication layer
├── zigbee_devices.h/c      # Device registry (64 devices)
├── zigbee_mgr.h/c          # Manager layer (pairing, control)
├── zigbee_zones.h/c        # Virtual zones 50-64
├── zigbee_relays.h/c       # Virtual relays 64-95
└── CMakeLists.txt          # Build configuration

test_linux/
├── zigbee_mock.h/c         # USB serial mock for testing
└── main_mock.c             # API endpoints (updated)

main/
├── main.c                  # 7 Zigbee API endpoints
└── CMakeLists.txt          # Dependencies

data/
└── zigbee.html             # Web interface
```

### Dependencies

**ESP32:**
```cmake
REQUIRES esp_driver_uart esp_driver_gpio driver freertos
```

**Linux Mock:**
```c
#include <termios.h>   // USB serial
#include <fcntl.h>     // File control
```

## Troubleshooting

### Coordinator Not Responding

**Symptom**: No devices found, connection fails

**Solutions:**
1. Check wiring (TX→RX, RX→TX, GND)
2. Verify 3.3V power (NOT 5V)
3. Confirm baud rate: 115200
4. Check coordinator firmware (Z-Stack 3.x or EmberZNet 6.x)
5. Monitor UART with logic analyzer

```bash
# Test UART on Linux:
screen /dev/ttyUSB0 115200
```

### Devices Won't Pair

**Symptom**: Pairing mode enabled but devices don't join

**Solutions:**
1. Verify coordinator supports Zigbee 3.0
2. Check network channel (some coordinators default to channel 11)
3. Reset device (hold button 10+ seconds until LED blinks)
4. Move device closer during pairing (<2m)
5. Check if network is full (64 device limit)

### Devices Offline After Reboot

**Symptom**: Devices show offline in web UI

**Solutions:**
1. Run discovery: `POST /api/zigbee/discover`
2. Wait 30 seconds for heartbeat timer
3. Power cycle battery devices
4. Check coordinator keeps network info in NVRAM

### Zone Not Triggering

**Symptom**: Sensor mapped but zone doesn't trigger

**Solutions:**
1. Verify zone mapping: `GET /api/zigbee/devices`
2. Check zone range (50-64 only)
3. Confirm sensor type supports CONTACT or OCCUPANCY capability
4. Test sensor manually (trigger motion or open door)
5. Check engine configuration for zone

### Mock Testing Issues

**Linux USB Serial:**
```bash
# Check for USB coordinator:
ls -la /dev/ttyUSB* /dev/ttyACM*

# Set permissions:
sudo chmod 666 /dev/ttyUSB0

# Override port:
export ZIGBEE_PORT=/dev/ttyUSB0
./sentinel_test
```

**Mock-Only Mode:**
If no USB coordinator found, mock automatically runs with 4 simulated devices:
- Living Room Light (0x1234) - relay 64
- Front Door Sensor (0x2345) - zone 50
- Hallway Motion (0x3456) - zone 51
- Kitchen Outlet (0x4567) - relay 65

## Performance Considerations

### Device Limits
- **Max Devices**: 64 (registry size)
- **Coordinator Limit**: Typically 20-50 direct children
- **Network Limit**: 200+ devices with routers

### Timing
- **Heartbeat**: 30 seconds
- **Command Timeout**: 5 seconds
- **Pairing Window**: 60 seconds (configurable)
- **Discovery**: 5-10 seconds (configurable)

### Memory Usage
- **Device Registry**: ~6 KB (64 devices × 100 bytes)
- **Protocol Buffer**: 2 KB UART buffer
- **Manager**: ~1 KB state

### Network Bandwidth
- **Idle**: ~1 command every 30s (heartbeat)
- **Active**: ~10-20 commands/second (burst)
- **Baud Rate**: 115200 bps = ~11.5 KB/s

## Security Considerations

### Zigbee 3.0 Security
- **Encryption**: AES-128 CCM
- **Authentication**: Install codes (optional)
- **Key Management**: Network key rotation
- **Trust Center**: Coordinator manages keys

### Best Practices
1. Use Zigbee 3.0 coordinators (not Zigbee 1.2)
2. Change default network key
3. Enable install code pairing for sensitive devices
4. Isolate Zigbee network from untrusted devices
5. Monitor for unauthorized devices

### Implementation Security
- UART communication not encrypted (internal bus)
- REST API requires HTTPS in production
- Zone/relay validation prevents out-of-range access
- Device removal requires authentication

## Advanced Configuration

### Coordinator Types

Set coordinator type during initialization:

```c
zigbee_config_t config = {
    .coordinator_type = ZIGBEE_COORDINATOR_CC2652P,
    .uart_num = UART_NUM_1,
    .tx_pin = 18,
    .rx_pin = 19,
    .baud_rate = 115200
};

zigbee_mgr_init(&config, my_callback);
```

### Custom Heartbeat Interval

```c
// Change from default 30s to 60s
zigbee_mgr_set_heartbeat_interval(60000);
```

### Event Callbacks

```c
void my_zigbee_callback(uint16_t short_addr, zigbee_event_t event) {
    switch (event) {
        case ZIGBEE_EVENT_DEVICE_JOINED:
            ESP_LOGI(TAG, "Device 0x%04x joined", short_addr);
            break;
        case ZIGBEE_EVENT_DEVICE_LEFT:
            ESP_LOGI(TAG, "Device 0x%04x left", short_addr);
            break;
        case ZIGBEE_EVENT_STATE_CHANGED:
            // Handle state change
            break;
    }
}

zigbee_mgr_init(NULL, my_zigbee_callback);
```

## Integration Examples

### Alarm Zone Automation

```c
// Front door sensor triggers alarm
if (zigbee_zone_get_state(50)) {
    // Zone 50 triggered (door opened)
    if (engine_is_armed()) {
        engine_trigger_alarm();
    }
}
```

### Relay Automation

```c
// Turn on porch light when alarm triggered
void alarm_triggered_handler(void) {
    // Relay 64 = front porch light
    zigbee_relay_set_state(64, true);
}
```

### Scene Control

```c
// "Goodnight" scene
void scene_goodnight(void) {
    // Turn off all lights (relays 64-70)
    for (int relay = 64; relay <= 70; relay++) {
        zigbee_relay_set_state(relay, false);
    }
    
    // Lock front door (relay 71)
    zigbee_mgr_set_lock(0x5678, true);
    
    // Arm alarm
    engine_set_mode(MODE_AWAY);
}
```

## Phase 2: Advanced Features (COMPLETE ✅)

### Implemented Features
- [x] RGB color control with HSV conversion
- [x] Color temperature control (2000K-6500K)
- [x] Smart lock control (lock/unlock)
- [x] Thermostat temperature setpoint
- [x] Battery level monitoring with alerts
- [x] Network statistics/diagnostics
- [x] Command success tracking
- [x] Web UI with color picker, temp slider, lock button
- [x] REST API endpoints for all features

### Phase 2 API Reference

#### RGB Color Control
```bash
# Set RGB color (0xRRGGBB format)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"color","value":16711680}'  # Red (0xFF0000)
```

#### Color Temperature Control
```bash
# Set color temperature in mireds (153-500 = 6500K-2000K)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"color_temp","value":250}'  # 4000K
```

#### Smart Lock Control
```bash
# Lock/unlock door
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"lock","value":"locked"}'  # or "unlocked"
```

#### Thermostat Control
```bash
# Set temperature setpoint (10-35°C)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"thermostat","value":21.5}'
```

#### Battery Monitoring
```bash
# Get battery level for device
curl http://esp32-ip/api/zigbee/device/0x1234/battery

# Response: {"battery_level":85,"low_battery":false}
```

#### Network Statistics
```bash
# Get network statistics
curl http://esp32-ip/api/zigbee/statistics

# Response:
# {
#   "total_devices": 8,
#   "online_devices": 7,
#   "offline_devices": 1,
#   "commands_sent": 523,
#   "commands_failed": 2,
#   "frames_received": 1847,
#   "low_battery_devices": 2,
#   "uptime_seconds": 14523
# }
```

### Testing
See [ZIGBEE_PHASE2_TESTING.md](ZIGBEE_PHASE2_TESTING.md) for complete hardware testing guide.

---

## Phase 3: Groups, Scenes, and Automations (COMPLETE ✅)

### Implemented Features
- [x] **Group Control**: Control up to 16 device groups simultaneously
- [x] **Scene Management**: Save and recall up to 32 device state snapshots
- [x] **Automation Rules**: Event-driven rules engine with 32 automation slots
- [x] Time-based triggers (cron-like scheduling)
- [x] Device/zone state change triggers
- [x] Conditional logic (time ranges, device states, battery levels)
- [x] Action sequences (device control, groups, scenes, delays)
- [x] Persistent JSON storage in SPIFFS
- [x] Web UI with tabs and modals
- [x] REST API endpoints for all features

### Architecture

```
┌──────────────────────────────────────────────────────┐
│           Zigbee Phase 3 Components                  │
├──────────────────────────────────────────────────────┤
│                                                      │
│  ┌─────────────────┐  ┌──────────────────┐         │
│  │  Groups         │  │  Scenes          │         │
│  │  (16 max)       │  │  (32 max)        │         │
│  │  ┌───────────┐  │  │  ┌────────────┐  │         │
│  │  │ Group 1   │  │  │  │  Scene 1   │  │         │
│  │  │ • Dev 1-N │  │  │  │  • States  │  │         │
│  │  │ • On/Off  │  │  │  │  • Recall  │  │         │
│  │  │ • Level   │  │  │  │            │  │         │
│  │  │ • Color   │  │  │  └────────────┘  │         │
│  │  └───────────┘  │  └──────────────────┘         │
│  └─────────────────┘                                │
│                                                      │
│  ┌─────────────────────────────────────────────┐   │
│  │  Automations (32 max)                       │   │
│  │  ┌──────────┐  ┌────────────┐  ┌─────────┐ │   │
│  │  │ Trigger  │→ │ Conditions │→ │ Actions │ │   │
│  │  │ • Time   │  │ • Time     │  │ • Device│ │   │
│  │  │ • Device │  │ • Device   │  │ • Group │ │   │
│  │  │ • Zone   │  │ • Zone     │  │ • Scene │ │   │
│  │  │ • Button │  │ • Battery  │  │ • Delay │ │   │
│  │  └──────────┘  └────────────┘  └─────────┘ │   │
│  │                                             │   │
│  │  Timer: 60-second interval checks          │   │
│  └─────────────────────────────────────────────┘   │
│                                                      │
│  Storage: /spiffs/zigbee_groups.json               │
│           /spiffs/zigbee_scenes.json               │
└──────────────────────────────────────────────────────┘
```

### Data Structures

#### Groups
```c
typedef struct {
    uint8_t group_id;           // 0-15
    char name[32];              // User-friendly name
    uint16_t devices[32];       // Device short addresses
    int device_count;           // Number of devices in group
    bool enabled;               // Active/inactive
} zigbee_group_t;
```

#### Scenes
```c
typedef struct {
    uint16_t short_addr;        // Device address
    bool has_on_off;            // Captured on/off state
    bool has_level;             // Captured brightness
    bool has_color;             // Captured RGB color
    bool has_color_temp;        // Captured color temperature
    bool on_off_value;          // On/off state
    uint8_t level_value;        // Brightness (0-255)
    uint32_t color_value;       // RGB (0xRRGGBB)
    uint16_t color_temp_value;  // Mireds (153-500)
} zigbee_scene_device_t;

typedef struct {
    uint8_t scene_id;           // 0-31
    char name[32];              // User-friendly name
    zigbee_scene_device_t devices[16];  // Device states
    int device_count;           // Number of devices in scene
    bool enabled;               // Active/inactive
} zigbee_scene_t;
```

#### Automations
```c
// Trigger types
typedef enum {
    ZIGBEE_TRIGGER_TIME,        // Time-based (hour, minute, days)
    ZIGBEE_TRIGGER_DEVICE_STATE,// Device state change
    ZIGBEE_TRIGGER_ZONE_STATE,  // Zone triggered/cleared
    ZIGBEE_TRIGGER_BUTTON,      // Button press
    ZIGBEE_TRIGGER_SCENE        // Scene activated
} zigbee_trigger_type_t;

// Condition types
typedef enum {
    ZIGBEE_CONDITION_TIME,      // Time range check
    ZIGBEE_CONDITION_DEVICE_STATE,  // Device on/off/level
    ZIGBEE_CONDITION_ZONE_STATE,    // Zone faulted/clear
    ZIGBEE_CONDITION_BATTERY_LOW    // Battery below threshold
} zigbee_condition_type_t;

// Action types
typedef enum {
    ZIGBEE_ACTION_DEVICE_ON_OFF,    // Turn device on/off
    ZIGBEE_ACTION_DEVICE_LEVEL,     // Set brightness
    ZIGBEE_ACTION_DEVICE_COLOR,     // Set RGB color
    ZIGBEE_ACTION_GROUP_CONTROL,    // Control group
    ZIGBEE_ACTION_SCENE_RECALL,     // Recall scene
    ZIGBEE_ACTION_DELAY             // Wait (ms)
} zigbee_action_type_t;

typedef struct {
    uint8_t automation_id;      // 0-31
    char name[32];              // User-friendly name
    bool enabled;               // Active/inactive
    
    zigbee_trigger_t trigger;   // What starts automation
    zigbee_condition_t conditions[4];  // What must be true (AND logic)
    int condition_count;
    zigbee_action_t actions[8]; // What to execute (sequential)
    int action_count;
} zigbee_automation_t;
```

### Phase 3 API Reference

#### Group Management

**List All Groups**
```bash
curl http://esp32-ip/api/zigbee/groups
# Response: [{"id":0,"name":"Living Room","enabled":true,"device_count":3},...]
```

**Create Group**
```bash
curl -X POST http://esp32-ip/api/zigbee/groups \
  -H "Content-Type: application/json" \
  -d '{"name":"Kitchen Lights"}'
# Response: {"success":true,"group_id":1}
```

**Delete Group**
```bash
curl -X DELETE http://esp32-ip/api/zigbee/group/1
# Response: {"success":true}
```

**Control Group**
```bash
# Turn group on/off
curl -X POST http://esp32-ip/api/zigbee/group/1/control \
  -H "Content-Type: application/json" \
  -d '{"action":"on_off","value":"on"}'

# Set group brightness
curl -X POST http://esp32-ip/api/zigbee/group/1/control \
  -H "Content-Type: application/json" \
  -d '{"action":"level","value":128}'

# Set group color
curl -X POST http://esp32-ip/api/zigbee/group/1/control \
  -H "Content-Type: application/json" \
  -d '{"action":"color","value":16711680}'  # Red
```

#### Scene Management

**List All Scenes**
```bash
curl http://esp32-ip/api/zigbee/scenes
# Response: [{"id":0,"name":"Movie Time","enabled":true,"device_count":5},...]
```

**Create Scene**
```bash
curl -X POST http://esp32-ip/api/zigbee/scenes \
  -H "Content-Type: application/json" \
  -d '{"name":"Good Night"}'
# Response: {"success":true,"scene_id":2}
```

**Delete Scene**
```bash
curl -X DELETE http://esp32-ip/api/zigbee/scene/2
# Response: {"success":true}
```

**Recall Scene**
```bash
# Restore all device states from scene
curl -X POST http://esp32-ip/api/zigbee/scene/2/recall
# Response: {"success":true}
```

#### Automation Examples

**Example 1: Sunset Lights**
- **Trigger**: Time = 18:00 (6 PM)
- **Condition**: None
- **Actions**: 
  1. Recall scene "Evening"
  2. Delay 2000ms
  3. Turn on group "Outdoor Lights"

**Example 2: Motion Detection**
- **Trigger**: Device state change (motion sensor)
- **Condition**: Time between 22:00-06:00
- **Actions**:
  1. Turn on device 0x1234 (hallway light)
  2. Set level to 50 (dim)
  3. Delay 300000ms (5 minutes)
  4. Turn off device 0x1234

**Example 3: Low Battery Alert**
- **Trigger**: Device state change
- **Condition**: Battery < 20%
- **Actions**:
  1. Recall scene "Alert" (flashes lights)

### Web UI Features

The Zigbee web interface (`/zigbee.html`) now includes:

**Tab Navigation**
- **Devices**: Original device management (Phase 1 & 2)
- **Groups**: Group creation and control
- **Scenes**: Scene creation and recall
- **Automations**: Automation management (placeholder UI)

**Groups Tab**
- Create new groups with custom names
- List all groups with device counts
- Control group on/off
- Delete groups

**Scenes Tab**
- Create new scenes with custom names
- List all scenes with device counts
- Recall scenes to restore device states
- Delete scenes

**Automations Tab**
- Placeholder for future automation UI
- Backend fully functional via API

### Implementation Files

**Backend Components** (~1,300 lines total)
- `components/zigbee/zigbee_groups.h/c` (404 lines)
- `components/zigbee/zigbee_scenes.h/c` (411 lines)
- `components/zigbee/zigbee_automations.h/c` (489 lines)

**REST API Integration**
- `main/main.c` - Added 6 REST endpoints

**Web UI**
- `data/zigbee.html` - Tab-based interface with modals

**Build Configuration**
- `components/zigbee/CMakeLists.txt` - Added esp_timer, storage dependencies

**Mock Testing**
- `test_linux/zigbee_mock.h/c` - Mock implementations for Linux testing

### Storage

**Groups**: `/spiffs/zigbee_groups.json`
```json
[
  {
    "id": 0,
    "name": "Living Room",
    "enabled": true,
    "devices": [4660, 4661, 4662]
  }
]
```

**Scenes**: `/spiffs/zigbee_scenes.json`
```json
[
  {
    "id": 0,
    "name": "Movie Time",
    "enabled": true,
    "devices": [
      {
        "addr": 4660,
        "on_off": false,
        "level": 50,
        "color": 16744448
      }
    ]
  }
]
```

**Automations**: In-memory only (save/load TODO for complex serialization)

### Testing

Phase 3 tests added to `test_linux/test_complete.sh`:
- Group creation and control
- Scene creation and recall
- REST API endpoint validation
- Web UI element verification

Run tests:
```bash
cd test_linux
./sentinel_test &
./test_complete.sh
```

---

## Future Enhancements (Phase 4)

### Planned Features
- [ ] Complete automation UI (visual rule builder)
- [ ] Zigbee network mapping/visualization
- [ ] OTA firmware updates for devices
- [ ] Binding support (direct device-to-device)
- [ ] Thermostat scheduling with automations
- [ ] Energy monitoring for outlets
- [ ] Window covering control (blinds, shades)
- [ ] Advanced automation triggers (sunrise/sunset, geofencing)

### Potential Improvements
- Multiple coordinator support
- Zigbee Green Power devices
- Thread/Matter border router integration
- Mesh network visualization
- Advanced security (install codes)
- Device profiles and templates
- Backup/restore for groups/scenes/automations

## References

### Documentation
- Zigbee 3.0 Specification: https://zigbeealliance.org/
- TI Z-Stack 3.x User Guide
- Silicon Labs EmberZNet Documentation
- ESP-IDF UART Driver: https://docs.espressif.com/

### Hardware
- TI CC2652P: https://www.ti.com/product/CC2652P
- KC868-A8: https://www.kincony.com/esp32-alarm-system.html

### Tools
- Zigbee2MQTT: https://www.zigbee2mqtt.io/
- Z2M Device Database: https://www.zigbee2mqtt.io/supported-devices/
- Wireshark Zigbee Plugin: https://wiki.wireshark.org/Zigbee

---

**Last Updated:** January 31, 2026  
**Version:** 3.0 (Phase 3 Complete - Groups, Scenes, Automations)  
**Author:** Sentinel Alarm System
