# Matter Controller Implementation

## Overview

Matter protocol support has been implemented for Sentinel-ESP, enabling control of Matter-certified devices including ESPHome devices with Matter support and commercial smart home products.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Sentinel-ESP Main System               │
│                                                          │
│  ┌────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │   Engine   │  │   Monitor   │  │   Web UI/API    │ │
│  └─────┬──────┘  └──────┬──────┘  └────────┬────────┘ │
│        │                │                   │          │
│        └────────────────┴───────────────────┘          │
│                         │                               │
│             ┌───────────▼──────────┐                    │
│             │ Matter Controller    │                    │
│             │  Component           │                    │
│             └───────────┬──────────┘                    │
└─────────────────────────┼───────────────────────────────┘
                          │ Matter Protocol
                          │ (UDP port 5540)
            ┌─────────────┼─────────────┐
            │             │             │
     ┌──────▼──────┐ ┌───▼────────┐ ┌─▼──────────┐
     │  ESPHome    │ │  Philips   │ │   Aqara    │
     │  Motion     │ │  Hue Bulb  │ │   Door     │
     │  Sensor     │ │            │ │   Sensor   │
     └─────────────┘ └────────────┘ └────────────┘
```

## Components

### 1. matter_controller.c
Core Matter controller initialization and device management:
- Device commissioning/pairing
- Device registry maintenance
- Connection management
- Event processing task

### 2. matter_clusters.c
Matter cluster implementations:
- **On/Off Cluster (0x0006)**: Switches, lights
- **Level Control (0x0008)**: Dimmable lights
- **Door Lock (0x0101)**: Smart locks
- **Occupancy Sensing (0x0406)**: Motion sensors
- **Boolean State (0x0045)**: Contact sensors
- **Temperature (0x0402)**: Temperature sensors

### 3. matter_commissioning.c
Device pairing functionality:
- QR code parsing (MT: format)
- On-network discovery
- BLE commissioning
- Commissioning window management

### 4. matter_zones.c
Virtual zone integration (zones 33-96):
- Maps Matter sensors to Sentinel zones
- Debounce filtering (3 readings)
- Alarm triggering when armed
- State tracking

### 5. matter_relays.c
Virtual relay integration (relays 8-31):
- Maps Matter switches/lights to Sentinel relays
- Unified control interface
- Alarm response automation
- Brightness control for dimmable lights

### 6. matter_storage.c
NVS persistence:
- Device configuration storage
- Zone/relay mappings
- Automatic load on boot

## Usage

### Initialization

```c
// In main/app_main.c
#include "matter_controller.h"

void app_main(void) {
    // ... existing initialization ...
    
    // Initialize Matter controller
    matter_controller_init();
    matter_zones_init();
    matter_relays_init();
    
    // Start Matter event processing
    matter_controller_start_task();
    
    // Load saved devices
    matter_load_config();
}
```

### Commissioning a Device

**Step 1: User scans QR code or enters pairing code**
```
QR Code format: MT:Y.K9QAO00KNY0648G00
Numeric format: 34970112332
```

**Step 2: Commission via web UI or API**
```c
int result = matter_commission_device("MT:Y.K9QAO00KNY0648G00", "Garage Motion");
if (result == 0) {
    printf("Device commissioned successfully\n");
}
```

**Step 3: Map to virtual zone or relay**
```c
// Map motion sensor to zone 33
matter_map_sensor_to_zone(node_id, 1, 33);

// Map smart switch to relay 8
matter_map_switch_to_relay(node_id, 1, 8);
```

### Controlling Devices

**Turn on/off switch:**
```c
matter_send_on_off(node_id, 1, true);  // Turn ON
matter_send_on_off(node_id, 1, false); // Turn OFF
```

**Set brightness (dimmable lights):**
```c
matter_send_brightness(node_id, 1, 127);  // 50% brightness
matter_send_brightness(node_id, 1, 254);  // Maximum brightness
```

**Lock/unlock door:**
```c
matter_send_lock_unlock(node_id, 1, true, NULL);   // Lock
matter_send_lock_unlock(node_id, 1, false, "1234"); // Unlock with PIN
```

### Alarm Integration

**Automatic trigger on motion (zone 33):**
```c
// Automatically handled by matter_zones.c
// When sensor triggers and system is armed:
// 1. Debounce filter (3 readings)
// 2. Call trigger_alarm(33, "Matter Motion Detected")
// 3. Engine handles entry delay / alarm
```

**Alarm response (turn on all lights):**
```c
// In engine.c when alarm triggers:
void engine_handle_alarm_triggered(void) {
    // ... existing siren activation ...
    
    // Activate all Matter lights
    matter_trigger_alarm_response();
}
```

## Web UI Integration

### REST API Endpoints (to be added)

**GET /api/matter/devices**
```json
{
  "devices": [
    {
      "node_id": "0x1234567890ABCDEF",
      "name": "Garage Motion",
      "device_type": "occupancy_sensor",
      "is_online": true,
      "virtual_zone_id": 33
    },
    {
      "node_id": "0xFEDCBA0987654321",
      "name": "Front Door Light",
      "device_type": "dimmable_light",
      "is_online": true,
      "virtual_relay_id": 8,
      "brightness": 254
    }
  ]
}
```

**POST /api/matter/commission**
```json
{
  "pairing_code": "MT:Y.K9QAO00KNY0648G00",
  "device_name": "Kitchen Motion"
}
```

**POST /api/matter/map_zone**
```json
{
  "node_id": "0x1234567890ABCDEF",
  "endpoint_id": 1,
  "zone_id": 34
}
```

**POST /api/matter/control**
```json
{
  "node_id": "0xFEDCBA0987654321",
  "endpoint_id": 1,
  "action": "on",
  "brightness": 200
}
```

**DELETE /api/matter/device/:node_id**
Removes device from fabric.

## Configuration

### sdkconfig Options

Add to `sdkconfig`:
```
CONFIG_ENABLE_CHIP_SHELL=n
CONFIG_ESP_MATTER_ENABLE=y
CONFIG_ESP_MATTER_CONTROLLER_ENABLE=y
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
```

### Memory Requirements

- **Flash**: ~300KB (Matter stack + controller)
- **RAM**: ~150KB (runtime buffers, connection state)
- **NVS**: ~16KB (device configuration storage)

## Device Compatibility

### Tested ESPHome Devices
- Motion sensors (PIR)
- Door/window contacts
- Smart switches
- LED strips/bulbs
- Temperature sensors

### Compatible Commercial Devices
- **Lighting**: Philips Hue, Nanoleaf, IKEA Trådfri
- **Sensors**: Aqara motion, Aqara door/window, Eve sensors
- **Locks**: Yale Assure, Schlage Encode
- **Thermostats**: Ecobee, Nest (Matter models)

## Example Use Cases

### Use Case 1: Wireless Zone Expansion
```
Physical Zones (0-31): Wired sensors via GPIO/I2C
Virtual Zones (33-96): Matter/ESPHome wireless sensors

Benefits:
- No wiring required
- Easy to add/remove sensors
- Battery-powered options
- Place sensors anywhere
```

### Use Case 2: Smart Lighting Response
```
When alarm triggers:
1. All Matter lights turn ON at 100%
2. Philips Hue changes to red
3. Outdoor floodlights activate
4. Interior lights flash (if supported)

When alarm clears:
- All lights return to previous state
```

### Use Case 3: Multi-Location Monitoring
```
Main Sentinel: Central alarm panel (wired zones)
Remote ESPs: Matter sensors in detached garage, shed, etc.

No wiring between buildings
All sensors monitored by main panel
```

## Troubleshooting

### Device Won't Commission
1. Ensure device is in pairing mode (factory reset)
2. Check WiFi connectivity (device and Sentinel on same network)
3. Verify pairing code is correct
4. Check Matter fabric isn't full (max 64 devices)

### Sensor Not Triggering Alarm
1. Verify zone mapping: `matter_get_devices()`
2. Check system is armed
3. Test sensor manually (wave hand, open door)
4. Check logs for debounce messages

### Device Shows Offline
1. Check device power and WiFi
2. Reboot device
3. Check Matter subscriptions are active
4. Verify network stability

## Future Enhancements

- [ ] Matter Thread support (requires border router)
- [ ] Color control for RGB lights
- [ ] Window covering control (blinds)
- [ ] Scene management
- [ ] Group commands (all lights on/off)
- [ ] Matter bridge mode (expose Sentinel as Matter device)

## References

- Matter Specification: https://csa-iot.org/developer-resource/specifications-download-request/
- ESP-IDF Matter: https://github.com/espressif/esp-matter
- ESPHome Matter: https://esphome.io/components/esp32_matter.html
