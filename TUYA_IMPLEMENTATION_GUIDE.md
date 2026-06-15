# Tuya Device Integration Guide

## Overview

The Sentinel system integrates with Tuya-compatible IoT devices through the built-in Tuya CBU WiFi module on the KC868-A8 hardware. This enables control and monitoring of switches, lights, sensors, locks, and other Tuya smart home devices directly from the alarm system.

### Key Features
- **Native Hardware Support**: Uses the KC868-A8's built-in Tuya CBU module (GPIO16/17)
- **Device Types**: Switches, lights (RGB/brightness), sensors (door/motion), locks, thermostats, curtains
- **Virtual Integration**: Sensors map to zones 65-96, outputs map to relays 32-63
- **Real-time Updates**: Bidirectional communication with device state synchronization
- **Web Management**: Responsive UI for pairing, control, and zone/relay mapping
- **REST API**: 7 endpoints for complete device management

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Sentinel Engine                      │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐       │
│  │ Zone 65-96 │  │Relay 32-63 │  │  Web UI    │       │
│  └─────┬──────┘  └──────┬─────┘  └──────┬─────┘       │
│        │                │                │              │
└────────┼────────────────┼────────────────┼──────────────┘
         │                │                │
    ┌────▼────────────────▼────────────────▼─────┐
    │         Tuya Manager (tuya_mgr)            │
    │  - Device registry (32 devices max)        │
    │  - Heartbeat timer (10s)                   │
    │  - Background RX task                      │
    │  - Event callbacks                         │
    └────────────────┬───────────────────────────┘
                     │
    ┌────────────────▼───────────────────────────┐
    │      Tuya Protocol (tuya_protocol)         │
    │  - UART communication (9600 baud)          │
    │  - Frame encoding/decoding                 │
    │  - Checksum validation                     │
    └────────────────┬───────────────────────────┘
                     │
              ┌──────▼──────┐
              │  GPIO16/17   │  Tuya CBU Module
              │   (RX/TX)    │  on KC868-A8
              └──────────────┘
```

## Hardware Setup

### KC868-A8 Tuya Module

The KC868-A8 comes with a Tuya CBU WiFi module connected via UART:
- **TX**: GPIO17 (ESP32 → Tuya)
- **RX**: GPIO16 (ESP32 ← Tuya)
- **Baud Rate**: 9600
- **Protocol**: Tuya MCU SDK v3

No additional hardware required - the Tuya module is already integrated.

### Wiring Diagram
```
┌─────────────────┐
│    ESP32-S3     │
│   (KC868-A8)    │
│                 │
│  GPIO17 (TX) ───┼───> RX  ┐
│                 │          │  Tuya CBU
│  GPIO16 (RX) <──┼────  TX │  WiFi Module
│                 │          │
│  GND ───────────┼──── GND │
│                 │          │
└─────────────────┘          ┘
```

## Device Types

### Output Devices (Control)
| Type | Description | Capabilities |
|------|-------------|-------------|
| **Switch** | On/Off relay | Switch |
| **Light** | Dimmable light | Switch, Brightness (0-100%) |
| **RGB Light** | Color LED | Switch, Brightness, RGB Color |
| **Socket** | Smart plug | Switch, Power monitoring |
| **Curtain** | Motor control | Position (0-100%) |
| **Thermostat** | HVAC control | Temperature setpoint, Mode |
| **Lock** | Smart lock | Lock/Unlock |

### Sensor Devices (Monitoring)
| Type | Description | States |
|------|-------------|--------|
| **Door Sensor** | Contact switch | Open/Closed, Battery % |
| **Motion Sensor** | PIR detector | Motion/Clear, Battery % |
| **Temperature** | Climate sensor | Temperature (°F/°C) |
| **Smoke Detector** | Fire alarm | Normal/Alarm, Battery % |
| **Water Leak** | Flood sensor | Dry/Wet, Battery % |

## Virtual Zone & Relay Allocation

### System-Wide Allocation
```
ZONES:
├── 1-32    Physical alarm zones (KC868-A8 hardware)
├── 33-64   ESPHome virtual zones (sensors)
└── 65-96   Tuya virtual zones (sensors)

RELAYS:
├── 1-7     Physical relays (KC868-A8 hardware)
├── 8-31    ESPHome virtual relays (outputs)
└── 32-63   Tuya virtual relays (outputs)
```

### Zone Mapping (65-96)
Tuya sensors (door, motion, smoke, water leak) automatically trigger virtual zones:
- Zone state updates in real-time
- Battery levels monitored
- Tamper detection supported
- Appears in zone status display

### Relay Mapping (32-63)
Tuya output devices (switches, lights, locks) can be controlled as relays:
- On/Off via relay control
- Brightness/color via device-specific API
- State synchronization bidirectional
- Appears in relay status display

## Configuration

### Initialization (main.c)

```c
#include "tuya_mgr.h"

void app_main(void) {
    // ... other initialization ...
    
    // Initialize Tuya manager
    tuya_mgr_init(NULL, NULL);  // NULL = default config
    
    // ... continue initialization ...
}
```

### No Additional Configuration Required

The Tuya integration is fully automatic:
- UART configured on first call
- Devices discovered via Smart Life app pairing
- No manual device registration needed
- State persists across reboots (future enhancement)

## Adding Devices

### Pairing Process

1. **Put Sentinel in Pairing Mode**
   - Open Tuya web UI at http://[device-ip]/tuya.html
   - Click "Enable Pairing Mode" (60 second window)

2. **Add Device via Smart Life App**
   - Open Smart Life/Tuya Smart app on phone
   - Tap "Add Device"
   - Follow app instructions (power cycle device, enter WiFi credentials)
   - Device appears in both app AND Sentinel

3. **Configure in Sentinel**
   - Device auto-appears in Tuya UI with friendly name
   - Map sensors to zones (65-96) for alarm integration
   - Map outputs to relays (32-63) for automation
   - Test device control (switch, brightness, etc.)

### Discovery Mode

```bash
# Trigger device discovery (10 second scan)
curl -X POST http://192.168.1.100/api/tuya/discover \
  -H "Content-Type: application/json" \
  -d '{"timeout_ms": 10000}'
```

## Web UI Usage

### Accessing Tuya Management
Navigate to: **http://[device-ip]/tuya.html**

### UI Features

#### Device Grid
- **Device Cards**: Shows all paired devices with:
  - Device name and type badge
  - Online/offline status indicator
  - Current state (on/off, open/closed, etc.)
  - Battery level (for sensors)
  - Control buttons (switches, brightness sliders)
  
#### Device Controls
- **Switches**: Toggle on/off with immediate feedback
- **Lights**: Brightness slider (0-100%), color picker (RGB lights)
- **Locks**: Lock/unlock button with confirmation
- **Sensors**: Read-only state display with icons

#### Zone/Relay Mapping
- **Map to Zone**: Assign sensor to virtual zone 65-96
  - Opens modal with zone number input
  - Validates range (65-96 only)
  - Updates in real-time
  
- **Map to Relay**: Assign output to virtual relay 32-63
  - Opens modal with relay number input
  - Validates range (32-63 only)
  - Bidirectional state sync enabled

#### Pairing Mode
- **Enable Pairing**: Opens 60-second pairing window
  - Shows countdown timer
  - Displays Smart Life app instructions
  - Auto-refreshes device list on completion

#### Connection Status
- **Status Indicator**: Top-right corner
  - Green: Tuya module connected
  - Red: Communication error
  - Pulsing: Activity indicator

### Auto-Refresh
Device states update every 5 seconds automatically. Manual refresh available via button.

## REST API

### 1. List Devices
```bash
GET /api/tuya/devices

Response:
{
  "connected": true,
  "count": 3,
  "devices": [
    {
      "id": "bf1234567890abcdef",
      "name": "Living Room Light",
      "type": "light",
      "state": "online",
      "capabilities": 3,  // Bitmask: switch=1, brightness=2, color=4
      "current_switch": "on",
      "current_brightness": 75,
      "zone_id": 0,
      "relay_id": 32
    },
    {
      "id": "bf0987654321fedcba",
      "name": "Front Door Sensor",
      "type": "door_sensor",
      "state": "online",
      "capabilities": 256,  // Battery reporting
      "current_contact": "closed",
      "current_battery": 95,
      "zone_id": 65,
      "relay_id": 0
    }
  ]
}
```

### 2. Discover Devices
```bash
POST /api/tuya/discover
Content-Type: application/json

{
  "timeout_ms": 10000  // Optional, default 10000ms
}

Response:
{
  "success": true,
  "message": "Discovery started"
}
```

### 3. Enable Pairing Mode
```bash
POST /api/tuya/pair
Content-Type: application/json

{
  "duration_sec": 60  // Optional, default 60 seconds
}

Response:
{
  "success": true,
  "message": "Pairing enabled for 60 seconds"
}
```

### 4. Remove Device
```bash
DELETE /api/tuya/device/{device_id}

Response:
{
  "success": true,
  "message": "Device removed"
}
```

### 5. Control Device
```bash
POST /api/tuya/device/{device_id}/control
Content-Type: application/json

# Switch on/off
{
  "action": "switch",
  "value": "on"  // or "off"
}

# Set brightness (0-100)
{
  "action": "brightness",
  "value": "75"
}

# Lock/unlock
{
  "action": "lock",
  "value": "locked"  // or "unlocked"
}

Response:
{
  "success": true,
  "message": "Device controlled"
}
```

### 6. Map to Zone
```bash
POST /api/tuya/device/{device_id}/map_zone
Content-Type: application/json

{
  "zone_id": 65  // Must be 65-96
}

Response:
{
  "success": true,
  "message": "Device mapped to zone 65"
}
```

### 7. Map to Relay
```bash
POST /api/tuya/device/{device_id}/map_relay
Content-Type: application/json

{
  "relay_id": 32  // Must be 32-63
}

Response:
{
  "success": true,
  "message": "Device mapped to relay 32"
}
```

## Programming Examples

### C - Device Control
```c
#include "tuya_mgr.h"

void turn_on_light(const char *device_id) {
    esp_err_t ret = tuya_mgr_set_switch(device_id, true);
    if (ret == ESP_OK) {
        printf("Light turned on\n");
    }
}

void set_brightness(const char *device_id, uint8_t level) {
    // level: 0-100
    esp_err_t ret = tuya_mgr_set_brightness(device_id, level);
    if (ret == ESP_OK) {
        printf("Brightness set to %d%%\n", level);
    }
}

void map_sensor_to_zone(const char *device_id, int zone_id) {
    // zone_id must be 65-96
    if (zone_id >= 65 && zone_id <= 96) {
        esp_err_t ret = tuya_mgr_map_to_zone(device_id, zone_id);
        if (ret == ESP_OK) {
            printf("Sensor mapped to zone %d\n", zone_id);
        }
    }
}
```

### C - Check Zone State (Engine Integration)
```c
#include "tuya_zones.h"

bool check_tuya_zone(int zone_id) {
    if (zone_id >= 65 && zone_id <= 96) {
        bool state = tuya_zone_get_state(zone_id);
        return state;  // true = triggered
    }
    return false;
}
```

### C - Control via Relay (Engine Integration)
```c
#include "tuya_relays.h"

void control_via_relay(int relay_id, bool on) {
    if (relay_id >= 32 && relay_id <= 63) {
        esp_err_t ret = tuya_relay_set_state(relay_id, on);
        if (ret == ESP_OK) {
            printf("Relay %d set to %s\n", relay_id, on ? "ON" : "OFF");
        }
    }
}
```

### JavaScript - Device List
```javascript
async function loadTuyaDevices() {
    const response = await fetch('/api/tuya/devices');
    const data = await response.json();
    
    console.log(`Connected: ${data.connected}`);
    console.log(`Device count: ${data.count}`);
    
    data.devices.forEach(device => {
        console.log(`${device.name} (${device.type}): ${device.state}`);
        
        if (device.zone_id > 0) {
            console.log(`  → Mapped to zone ${device.zone_id}`);
        }
        
        if (device.relay_id > 0) {
            console.log(`  → Mapped to relay ${device.relay_id}`);
        }
    });
}
```

### JavaScript - Device Control
```javascript
async function controlDevice(deviceId, action, value) {
    const response = await fetch(`/api/tuya/device/${deviceId}/control`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, value })
    });
    
    const result = await response.json();
    return result.success;
}

// Examples:
await controlDevice('bf1234567890abcdef', 'switch', 'on');
await controlDevice('bf1234567890abcdef', 'brightness', '50');
```

### Python - Automation Script
```python
import requests
import json

BASE_URL = 'http://192.168.1.100'

def get_devices():
    response = requests.get(f'{BASE_URL}/api/tuya/devices')
    return response.json()

def turn_on_device(device_id):
    payload = {'action': 'switch', 'value': 'on'}
    response = requests.post(
        f'{BASE_URL}/api/tuya/device/{device_id}/control',
        json=payload
    )
    return response.json()

def map_to_zone(device_id, zone_id):
    payload = {'zone_id': zone_id}
    response = requests.post(
        f'{BASE_URL}/api/tuya/device/{device_id}/map_zone',
        json=payload
    )
    return response.json()

# Example: Turn on all lights
devices = get_devices()
for device in devices['devices']:
    if device['type'] == 'light':
        turn_on_device(device['id'])
        print(f"Turned on {device['name']}")
```

## Mock Testing (Linux)

### Building with Mock
```bash
cd /home/kjgerhart/sentinel-esp/test_linux
make clean && make
./sentinel_test
```

### Mock Devices
The mock implementation includes 3 pre-configured test devices:

1. **Living Room Light** (bf1234567890abcdef)
   - Type: Light
   - Capabilities: Switch, Brightness
   - Mapped to: Relay 32
   - Initial state: On, 75% brightness

2. **Front Door Sensor** (bf0987654321fedcba)
   - Type: Door Sensor
   - Capabilities: Contact, Battery
   - Mapped to: Zone 65
   - Initial state: Closed, 95% battery

3. **Hallway Motion** (bf1111222233334444)
   - Type: Motion Sensor
   - Capabilities: Motion, Battery
   - Mapped to: Zone 66
   - Initial state: Clear, 88% battery

### Testing with cURL
```bash
# List mock devices
curl http://localhost:8000/api/tuya/devices | jq

# Control light
curl -X POST http://localhost:8000/api/tuya/device/bf1234567890abcdef/control \
  -H "Content-Type: application/json" \
  -d '{"action":"switch","value":"on"}'

# Set brightness
curl -X POST http://localhost:8000/api/tuya/device/bf1234567890abcdef/control \
  -H "Content-Type: application/json" \
  -d '{"action":"brightness","value":"50"}'

# Map to zone
curl -X POST http://localhost:8000/api/tuya/device/bf0987654321fedcba/map_zone \
  -H "Content-Type: application/json" \
  -d '{"zone_id":70}'

# Open web UI
xdg-open http://localhost:8000/tuya.html
```

### Automated Testing
```bash
cd test_linux
./test_complete.sh
```

Tests include:
- Device list retrieval
- Discovery and pairing
- Device control (switch, brightness)
- Zone/relay mapping (validation)
- Web UI page loading

## Troubleshooting

### Device Not Appearing

**Symptom**: Device paired in Smart Life but not in Sentinel

**Solution**:
1. Check Tuya module connection: `curl http://[device-ip]/api/tuya/devices`
2. Look for `"connected": false` in response
3. Verify GPIO16/17 wiring (KC868-A8 should be pre-wired)
4. Check UART initialization: Look for `[TUYA] Tuya manager initialized` in logs
5. Try discovery: `POST /api/tuya/discover`

### Device Shows Offline

**Symptom**: Device appears in list but state shows "offline"

**Solution**:
1. Verify device has power and WiFi connection
2. Check device in Smart Life app - should show online there too
3. Device may be in wrong WiFi network (Sentinel and device must be on same LAN)
4. Try removing and re-pairing device
5. Check heartbeat in logs: Should see periodic communication

### Control Commands Not Working

**Symptom**: API returns success but device doesn't respond

**Solution**:
1. Check device capabilities: Not all devices support all commands
   ```bash
   curl http://[device-ip]/api/tuya/devices | jq '.devices[] | {name, capabilities}'
   ```
2. Verify correct device ID in API call
3. Check device state is "online"
4. Try controlling from Smart Life app to confirm device operational
5. Review Tuya protocol logs for communication errors

### Zone/Relay Mapping Not Working

**Symptom**: Mapped but engine doesn't see sensor triggers

**Solution**:
1. Verify zone ID in range 65-96:
   ```bash
   curl http://[device-ip]/api/tuya/devices | jq '.devices[] | {name, zone_id}'
   ```
2. Check zone status page: http://[device-ip]/zstatus.html
3. Look for Tuya zones (65-96) in zone list
4. Test sensor trigger (open door, wave at motion sensor)
5. Check engine logs for zone updates

### Build Errors

**Symptom**: Compilation fails with UART or GPIO errors

**Solution**:
1. Verify CMakeLists.txt has `esp_driver_uart` in REQUIRES:
   ```cmake
   idf_component_register(
       SRCS "tuya_protocol.c" "tuya_devices.c" "tuya_mgr.c"
       INCLUDE_DIRS "."
       REQUIRES esp_driver_uart driver
   )
   ```
2. Clean build: `idf.py fullclean && idf.py build`
3. Check ESP-IDF version: Requires v5.0+
4. Update esp_driver_uart component: `idf.py update-dependencies`

### Mock Testing Issues

**Symptom**: Mock build fails or devices don't appear

**Solution**:
1. Check tuya_mock.o in Makefile OBJ list
2. Verify #include "tuya_mock.h" in main_mock.c
3. Ensure tuya_mgr_init() called in main()
4. Check for missing stdint.h include
5. Rebuild: `make clean && make`

## Protocol Details

### UART Frame Structure
```
┌────────┬─────────┬─────────┬────────┬──────────┬──────────┐
│ Header │ Version │ Command │ Length │   Data   │ Checksum │
│ 0x55AA │  0x03   │  1 byte │ 2 bytes│  N bytes │  1 byte  │
└────────┴─────────┴─────────┴────────┴──────────┴──────────┘
```

### Command Types
| Command | Direction | Description |
|---------|-----------|-------------|
| 0x00 | → Tuya | Heartbeat |
| 0x01 | → Tuya | Product query |
| 0x02 | ← Tuya | Product info |
| 0x04 | → Tuya | WiFi reset |
| 0x05 | ← Tuya | WiFi status |
| 0x06 | ← Tuya | Status report |
| 0x07 | → Tuya | Control command |
| 0x08 | ← Tuya | State query response |

### Checksum Calculation
```c
uint8_t calculate_checksum(uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}
```

### DP (Data Point) Mapping
Common DP IDs for device control:
- **DP1**: Switch state (bool)
- **DP2**: Brightness (0-1000)
- **DP3**: Color temperature (mireds)
- **DP5**: RGB color (0xRRGGBB)
- **DP101**: Contact state (bool)
- **DP102**: Motion detection (bool)
- **DP103**: Battery level (0-100)

## Best Practices

### 1. Device Naming
- Use descriptive names: "Front Door Sensor" not "Sensor 1"
- Include location: "Kitchen Light", "Bedroom Motion"
- Avoid special characters in names
- Keep names under 32 characters

### 2. Zone Allocation
- Document zone assignments in config
- Use sequential zones: 65-70 for doors, 71-80 for motion, etc.
- Leave gaps for future expansion
- Update zone labels in UI after mapping

### 3. Relay Allocation
- Group by type: 32-40 lights, 41-50 locks, etc.
- Match to physical relay functions when possible
- Document relay → device mapping
- Test relay control before automation

### 4. Power Management
- Monitor battery levels on sensors (check weekly)
- Set up alerts for low battery (<20%)
- Keep spare batteries for sensors
- Consider AC-powered devices for critical sensors

### 5. Network Reliability
- Place WiFi access points strategically
- Use 2.4GHz WiFi for better range
- Avoid congested WiFi channels
- Monitor device online/offline status

### 6. Security
- Change default Tuya app passwords
- Use WPA3 WiFi if available
- Keep Smart Life app updated
- Regularly review device list for unknown devices

## Performance

### Resource Usage (ESP32)
- **Memory**: ~8KB RAM per device
- **CPU**: <1% background (heartbeat + RX task)
- **Flash**: ~30KB code + device storage
- **UART**: 9600 baud (very low bandwidth)

### Limits
- **Max Devices**: 32 (registry limit)
- **Max Zones**: 32 (zones 65-96)
- **Max Relays**: 32 (relays 32-63)
- **Command Rate**: ~10 commands/second
- **State Updates**: 1-2 seconds latency

### Optimization Tips
- Use zone mapping for sensors (faster than polling)
- Batch control commands (avoid rapid-fire API calls)
- Enable device caching (reduces UART traffic)
- Monitor heartbeat intervals (adjust if needed)

## Future Enhancements

### Planned Features
- [ ] Device persistence (survive reboots)
- [ ] RGB color control (full spectrum)
- [ ] Color temperature control (warm/cool white)
- [ ] Scene support (multi-device control)
- [ ] Schedule/automation rules
- [ ] Device grouping
- [ ] Power monitoring (for sockets)
- [ ] Advanced sensor types (smoke, CO, water leak)
- [ ] Device firmware OTA updates
- [ ] Cloud sync disable (local-only mode)

### Integration Improvements
- [ ] Home Assistant MQTT discovery
- [ ] Alexa/Google Home proxy
- [ ] Direct Tuya cloud API (bypass Smart Life)
- [ ] Enhanced ESPHome device support
- [ ] Camera integration with Tuya devices

## Support

### Documentation
- **Tuya MCU SDK**: https://developer.tuya.com/en/docs/iot/mcu-sdk
- **Smart Life App**: https://www.tuya.com/smart-life
- **KC868-A8 Manual**: Check manufacturer documentation

### Community
- GitHub Issues: Report bugs and request features
- Forum: Share configurations and automation ideas

### Logs
Enable debug logging for detailed protocol information:
```c
// In sdkconfig or menuconfig
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

View logs:
```bash
idf.py monitor
# Or
screen /dev/ttyUSB0 115200
```

## Appendix: Device Capability Bitmasks

```c
// Capability flags (bitmask)
#define TUYA_CAP_SWITCH        (1 << 0)  // 0x001 - On/Off control
#define TUYA_CAP_BRIGHTNESS    (1 << 1)  // 0x002 - Brightness 0-100
#define TUYA_CAP_COLOR         (1 << 2)  // 0x004 - RGB color
#define TUYA_CAP_COLOR_TEMP    (1 << 3)  // 0x008 - Color temperature
#define TUYA_CAP_POSITION      (1 << 4)  // 0x010 - Position (curtains)
#define TUYA_CAP_LOCK          (1 << 5)  // 0x020 - Lock/unlock
#define TUYA_CAP_TEMPERATURE   (1 << 6)  // 0x040 - Temperature reading
#define TUYA_CAP_HUMIDITY      (1 << 7)  // 0x080 - Humidity reading
#define TUYA_CAP_BATTERY       (1 << 8)  // 0x100 - Battery level
#define TUYA_CAP_TAMPER        (1 << 9)  // 0x200 - Tamper detection
```

Example combinations:
- **Simple Switch**: 0x001 (switch only)
- **Dimmable Light**: 0x003 (switch + brightness)
- **RGB Light**: 0x007 (switch + brightness + color)
- **Smart Lock**: 0x121 (switch + lock + battery)
- **Door Sensor**: 0x301 (switch + battery + tamper)

---

**Last Updated**: January 31, 2026  
**Version**: 1.0  
**Status**: Phase 1 Complete ✅
