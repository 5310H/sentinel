# Tuya & Zigbee Support - Implementation Plan

**Date:** January 31, 2026  
**Goal:** Add support for Tuya devices (KC868-A8 native) and Zigbee devices as Matter alternatives

---

## Executive Summary

The KC868-A8 has a built-in **Tuya WiFi module** (CBU chip) that can be leveraged for Tuya device integration without additional hardware. Zigbee support would require adding a Zigbee coordinator chip (ESP32-C6 or external Zigbee module) but provides a solid alternative to Matter for commercial smart home devices.

---

## Part 1: Tuya Device Integration

### Hardware Overview (KC868-A8)

**Built-in Tuya Module:**
```
Tuya CBU WiFi Module (Pre-installed)
├─ RX: GPIO17 (connected to ESP32)
├─ TX: GPIO16 (connected to ESP32)
├─ Network Button: P28
└─ Network LED: P16
```

**Benefits:**
- ✅ Already on-board (no additional hardware)
- ✅ UART communication (simple protocol)
- ✅ Supports Tuya Smart/Smart Life apps
- ✅ Can control other Tuya devices via cloud
- ✅ Local LAN control option

### Tuya Integration Architecture

```
┌─────────────────────────────────────────────────┐
│              Sentinel ESP32-S3                  │
│  ┌────────────────────────────────────────────┐ │
│  │         Tuya Component                     │ │
│  │  • UART protocol handler (GPIO16/17)      │ │
│  │  • Device discovery                        │ │
│  │  • State management                        │ │
│  │  • Virtual zone/relay mapping              │ │
│  └────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
                    ↕ UART
┌─────────────────────────────────────────────────┐
│           Tuya CBU WiFi Module                  │
│  • Tuya Cloud connection                        │
│  • Local LAN protocol                           │
│  • Tuya device pairing                          │
└─────────────────────────────────────────────────┘
                    ↕ WiFi/Cloud
┌─────────────────────────────────────────────────┐
│           Tuya Smart Devices                    │
│  • Switches, lights, sensors                    │
│  • Locks, cameras, alarms                       │
│  • Thermostats, curtains, etc.                  │
└─────────────────────────────────────────────────┘
```

### Tuya Protocol (MCU SDK)

Tuya provides an **MCU SDK** for UART communication:

**Message Format:**
```c
// Tuya UART Protocol Frame
typedef struct {
    uint8_t header[2];      // 0x55 0xAA
    uint8_t version;        // 0x03
    uint8_t command;        // Command type
    uint16_t length;        // Data length (big endian)
    uint8_t data[N];        // Payload
    uint8_t checksum;       // Sum of all bytes
} tuya_frame_t;
```

**Common Commands:**
- `0x00` - Heartbeat
- `0x01` - Query product info
- `0x02` - Query working mode
- `0x06` - Send status update
- `0x07` - Receive status from Tuya cloud
- `0x08` - Query device state
- `0x24` - Local time sync

### Implementation Components

#### 1. Tuya Component Structure
```
components/tuya/
├── tuya_mgr.h          - Public API
├── tuya_mgr.c          - Manager implementation
├── tuya_protocol.h     - UART protocol definitions
├── tuya_protocol.c     - Protocol encode/decode
├── tuya_devices.h      - Device type definitions
├── tuya_devices.c      - Device management
├── tuya_zones.c        - Virtual zone mapping (33-96)
├── tuya_relays.c       - Virtual relay mapping (32-63)
└── CMakeLists.txt      - Build configuration
```

#### 2. Key Features

**Device Discovery:**
```c
// Auto-discover Tuya devices on network
int tuya_discover_devices(void);

// Get list of all Tuya devices
int tuya_get_devices(tuya_device_t *devices, int max, int *count);
```

**Device Control:**
```c
// Control Tuya switch/light
int tuya_set_switch(const char *device_id, bool on);

// Control Tuya dimmer
int tuya_set_brightness(const char *device_id, uint8_t level);

// Lock/unlock Tuya lock
int tuya_set_lock(const char *device_id, bool lock);
```

**Sensor Monitoring:**
```c
// Map Tuya sensor to virtual zone
int tuya_map_sensor_to_zone(const char *device_id, int zone_id);

// Sensor state callback
void tuya_sensor_callback(const char *device_id, bool triggered);
```

**Virtual Relays:**
```c
// Map Tuya output device to virtual relay (32-63)
int tuya_map_device_to_relay(const char *device_id, int relay_id);
```

#### 3. Configuration Storage

**File:** `/spiffs/tuya.json` or `/sd/tuya.json`

```json
{
  "devices": [
    {
      "id": "bf1234567890abcdef",
      "name": "Living Room Light",
      "type": "light",
      "enabled": true,
      "virtual_relay_id": 32,
      "capabilities": ["switch", "brightness", "color"]
    },
    {
      "id": "bf0987654321fedcba",
      "name": "Front Door Sensor",
      "type": "door_sensor",
      "enabled": true,
      "virtual_zone_id": 65,
      "capabilities": ["contact"]
    }
  ],
  "settings": {
    "local_control": true,
    "cloud_enabled": true,
    "polling_interval_ms": 1000
  }
}
```

#### 4. API Endpoints

**GET /api/tuya/devices** - List all Tuya devices
**POST /api/tuya/discover** - Scan for new devices
**POST /api/tuya/pair** - Pair device using Smart Life app
**DELETE /api/tuya/device/:id** - Remove device
**POST /api/tuya/device/:id/map_zone** - Map sensor to zone
**POST /api/tuya/device/:id/map_relay** - Map output to relay
**POST /api/tuya/device/:id/control** - Send command

#### 5. Web UI Integration

**Page:** `data/tuya.html`

Features:
- List all Tuya devices with status
- Discover and pair new devices
- Map sensors to virtual zones (65-96)
- Map outputs to virtual relays (32-63)
- Test device control
- View device capabilities

---

## Part 2: Zigbee Support

### Hardware Requirements

**Option 1: ESP32-C6 (Preferred)**
```
ESP32-C6 Module
├─ Built-in Zigbee 3.0 coordinator
├─ WiFi 6 + Thread
├─ Connects to KC868-A8 via UART
└─ Acts as Zigbee gateway
```

**Option 2: External Zigbee Coordinator**
```
Silicon Labs EZSP Coordinator (CC2652P)
├─ Connected via UART to ESP32
├─ Open-source firmware (Z-Stack)
├─ Zigbee2MQTT compatible
└─ Community support
```

**Recommendation:** Use **Zigbee2MQTT** bridge approach (simplest)

### Zigbee Architecture

```
┌─────────────────────────────────────────────────┐
│              Sentinel ESP32-S3                  │
│  ┌────────────────────────────────────────────┐ │
│  │      Zigbee Component (MQTT Bridge)        │ │
│  │  • Subscribe to Zigbee2MQTT topics         │ │
│  │  • Device state tracking                   │ │
│  │  • Virtual zone/relay mapping              │ │
│  │  • Command publishing                      │ │
│  └────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
                    ↕ MQTT
┌─────────────────────────────────────────────────┐
│           Zigbee2MQTT (External)                │
│  • Runs on Raspberry Pi / Docker                │
│  • Zigbee coordinator connected via USB         │
│  • Translates Zigbee ↔ MQTT                     │
└─────────────────────────────────────────────────┘
                    ↕ Zigbee
┌─────────────────────────────────────────────────┐
│           Zigbee Devices                        │
│  • Aqara sensors (motion, door, temp)           │
│  • Philips Hue bulbs                            │
│  • IKEA Tradfri switches                        │
│  • Tuya Zigbee devices                          │
└─────────────────────────────────────────────────┘
```

### Zigbee Integration Components

#### 1. Zigbee Component Structure
```
components/zigbee_mqtt/
├── zigbee_mgr.h        - Public API
├── zigbee_mgr.c        - Manager implementation
├── zigbee_devices.h    - Device definitions
├── zigbee_devices.c    - Device management
├── zigbee_mqtt.h       - MQTT bridge protocol
├── zigbee_mqtt.c       - MQTT message handling
├── zigbee_zones.c      - Virtual zone mapping (33-64)
├── zigbee_relays.c     - Virtual relay mapping (64-95)
└── CMakeLists.txt      - Build configuration
```

#### 2. MQTT Topics (Zigbee2MQTT)

**Subscribe Topics:**
```
zigbee2mqtt/bridge/devices      - Device list
zigbee2mqtt/+                   - All device updates
zigbee2mqtt/FrontDoorSensor     - Specific device
```

**Publish Topics:**
```
zigbee2mqtt/LivingRoomLight/set   - Control device
zigbee2mqtt/bridge/config/permit_join - Enable pairing
```

**Message Format:**
```json
{
  "battery": 100,
  "contact": false,
  "linkquality": 120,
  "tamper": false,
  "voltage": 3000
}
```

#### 3. Device Management

**Discovery:**
```c
// Subscribe to Zigbee2MQTT bridge
int zigbee_init(const char *mqtt_broker);

// Get device list
int zigbee_get_devices(zigbee_device_t *devices, int max, int *count);

// Enable pairing mode (60 seconds)
int zigbee_permit_join(bool enable);
```

**Device Control:**
```c
// Control Zigbee switch/light
int zigbee_set_switch(const char *friendly_name, bool on);

// Set brightness
int zigbee_set_brightness(const char *friendly_name, uint8_t level);

// Set color temperature
int zigbee_set_color_temp(const char *friendly_name, uint16_t mireds);
```

**Sensor Mapping:**
```c
// Map Zigbee sensor to virtual zone
int zigbee_map_sensor_to_zone(const char *friendly_name, int zone_id);

// Callback when sensor triggers
void zigbee_sensor_callback(const char *friendly_name, bool state);
```

#### 4. Configuration

**File:** `/spiffs/zigbee.json`

```json
{
  "mqtt_broker": "192.168.1.100:1883",
  "mqtt_user": "sentinel",
  "mqtt_password": "password",
  "base_topic": "zigbee2mqtt",
  "devices": [
    {
      "friendly_name": "Front Door Sensor",
      "ieee_address": "0x00158d0001a2b3c4",
      "type": "contact",
      "manufacturer": "Aqara",
      "model": "MCCGQ11LM",
      "enabled": true,
      "virtual_zone_id": 50
    },
    {
      "friendly_name": "Living Room Light",
      "ieee_address": "0x001788010a1b2c3d",
      "type": "light",
      "manufacturer": "Philips",
      "model": "Hue White",
      "enabled": true,
      "virtual_relay_id": 70
    }
  ]
}
```

#### 5. API Endpoints

**GET /api/zigbee/devices** - List all Zigbee devices
**POST /api/zigbee/permit_join** - Enable pairing mode
**DELETE /api/zigbee/device/:name** - Remove device
**POST /api/zigbee/device/:name/map_zone** - Map sensor
**POST /api/zigbee/device/:name/map_relay** - Map output
**POST /api/zigbee/device/:name/control** - Send command
**GET /api/zigbee/status** - Check Zigbee2MQTT connection

#### 6. Web UI

**Page:** `data/zigbee.html`

Features:
- List all Zigbee devices with battery/signal
- Enable pairing mode (60 second countdown)
- Map sensors to virtual zones (50-64)
- Map outputs to virtual relays (70-95)
- Test device control
- View device information (manufacturer, model, firmware)

---

## Virtual Zone/Relay Allocation

### Zone Mapping Strategy
```
Zones 1-32:   Physical wired zones (existing)
Zones 33-49:  ESPHome/Matter devices (existing)
Zones 50-64:  Zigbee devices (NEW)
Zones 65-96:  Tuya devices (NEW)
```

### Relay Mapping Strategy
```
Relays 1-7:   Physical relays (existing)
Relays 8-31:  ESPHome/Matter devices (existing)
Relays 32-63: Tuya devices (NEW)
Relays 64-95: Zigbee devices (NEW)
```

---

## Implementation Priority

### Phase 1: Tuya Support (Week 1-2)
**Effort:** ~40 hours

- [ ] Day 1-2: Implement Tuya UART protocol
- [ ] Day 3-4: Device discovery and pairing
- [ ] Day 5-6: Sensor mapping (zones 65-96)
- [ ] Day 7-8: Relay mapping (relays 32-63)
- [ ] Day 9-10: Web UI and API endpoints

**Benefits:**
- ✅ Uses existing KC868-A8 hardware
- ✅ Access to huge Tuya ecosystem
- ✅ Compatible with Smart Life app
- ✅ Local + cloud control

### Phase 2: Zigbee Support (Week 3-4)
**Effort:** ~30 hours

- [ ] Day 1-2: MQTT bridge implementation
- [ ] Day 3-4: Device management
- [ ] Day 5-6: Sensor mapping (zones 50-64)
- [ ] Day 7-8: Relay mapping (relays 64-95)
- [ ] Day 9-10: Web UI and testing

**Benefits:**
- ✅ Access to Aqara, Philips Hue, IKEA devices
- ✅ Mature ecosystem (Zigbee 3.0)
- ✅ Low power (battery sensors)
- ✅ Local control (no cloud required)

---

## Advantages Over Matter

### Matter Limitations (Current State)
- ❌ SDK requires 15-20GB disk space
- ❌ Complex build process
- ❌ Limited device availability (2026)
- ❌ Expensive certification
- ❌ Still evolving standard

### Tuya Advantages
- ✅ Hardware already on KC868-A8
- ✅ Massive device ecosystem (millions of products)
- ✅ Simple UART protocol
- ✅ Works with Smart Life app
- ✅ Cheap devices ($5-20)

### Zigbee Advantages
- ✅ Mature, stable protocol (20+ years)
- ✅ Huge device selection (Aqara, Hue, IKEA, Tuya)
- ✅ Low power (2-year battery life)
- ✅ Local control (no cloud)
- ✅ Mesh networking (self-healing)

---

## Development Roadmap

### Milestone 1: Tuya Basic Support (2 weeks)
- Tuya UART communication
- Device discovery
- Switch/sensor support
- Zone/relay mapping
- Basic web UI

### Milestone 2: Tuya Advanced Features (1 week)
- Dimmable lights
- Color control
- Door locks
- Temperature sensors
- Scene support

### Milestone 3: Zigbee Basic Support (2 weeks)
- MQTT bridge setup
- Device discovery
- Switch/sensor support
- Zone/relay mapping
- Basic web UI

### Milestone 4: Zigbee Advanced Features (1 week)
- Color lights (Hue)
- Groups and scenes
- Binding support
- OTA updates
- Network map visualization

---

## User Experience

### Tuya Device Setup
```
1. User opens Sentinel web UI
2. Navigate to "Tuya Devices"
3. Click "Pair Device"
4. Follow Smart Life app pairing
5. Device appears in Sentinel
6. Map to zone or relay
7. Test and save
```

### Zigbee Device Setup
```
1. User opens Sentinel web UI
2. Navigate to "Zigbee Devices"
3. Click "Enable Pairing" (60 seconds)
4. Press button on Zigbee device
5. Device auto-discovered
6. Map to zone or relay
7. Test and save
```

---

## Comparison Table

| Feature | Tuya | Zigbee | Matter | ESPHome |
|---------|------|--------|--------|---------|
| **Hardware Required** | ✅ Built-in | ⚠️ USB dongle | ❌ 20GB SDK | ✅ ESP devices |
| **Device Cost** | 💰 $5-20 | 💰 $10-50 | 💰💰 $30-100 | 💰 $5-15 |
| **Setup Complexity** | ⭐⭐ Easy | ⭐⭐⭐ Medium | ⭐⭐⭐⭐⭐ Hard | ⭐⭐ Easy |
| **Device Selection** | 🌟 Huge | 🌟 Huge | ⭐ Limited | ⭐⭐ DIY |
| **Local Control** | ⚠️ Cloud+Local | ✅ Local | ✅ Local | ✅ Local |
| **Battery Sensors** | ✅ Yes | ✅ Yes (2yr) | ✅ Yes | ❌ WiFi power |
| **Mesh Network** | ❌ No | ✅ Yes | ✅ Yes | ❌ No |
| **Vendor Lock-in** | ⚠️ Tuya cloud | ✅ Open | ✅ Open | ✅ Open |

---

## Recommended Approach

### Short Term (1-2 months)
1. **Implement Tuya support** - Uses existing hardware
2. **Test with common devices** - Lights, sensors, locks
3. **Deploy to production** - Real-world testing

### Medium Term (3-6 months)
1. **Add Zigbee support** - Requires USB coordinator
2. **Integrate both protocols** - Unified UI
3. **Performance optimization** - Reduce latency

### Long Term (6-12 months)
1. **Monitor Matter ecosystem** - Wait for maturity
2. **Evaluate Matter integration** - When SDK improves
3. **Multi-protocol support** - Tuya + Zigbee + Matter

---

## Cost Analysis

### Tuya Implementation
- **Hardware:** $0 (already on KC868-A8)
- **Development:** ~40 hours
- **Testing Devices:** $50-100 (switches, sensors)
- **Total:** ~$100

### Zigbee Implementation
- **Hardware:** $25 (USB Zigbee coordinator)
- **Development:** ~30 hours
- **Testing Devices:** $100-150 (Aqara sensors, Hue bulbs)
- **Total:** ~$175

### Matter (for comparison)
- **Hardware:** $0 (ESP32 capable)
- **Development:** ~60 hours (complexity)
- **SDK Setup:** 20GB disk, 2-3 hours
- **Testing Devices:** $200-300 (limited selection)
- **Total:** ~$300

**Conclusion:** Tuya + Zigbee = **$275**, Much cheaper and faster than Matter

---

## Next Steps

### To Start Tuya Implementation:

1. **Hardware Test:**
   ```bash
   # Test Tuya UART on KC868-A8
   # GPIO16=TX, GPIO17=RX
   esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
     --baud 115200 --before default_reset --after hard_reset \
     write_flash 0x0 tuya_test.bin
   ```

2. **Create Component:**
   ```bash
   cd components
   mkdir tuya
   cd tuya
   touch tuya_mgr.h tuya_mgr.c tuya_protocol.h tuya_protocol.c
   ```

3. **Implement Protocol:**
   - UART driver setup
   - Frame encode/decode
   - Command handlers
   - Device registry

4. **Test with Device:**
   - Tuya smart plug ($8 on Amazon)
   - Pair via Smart Life app
   - Control from Sentinel

### To Start Zigbee Implementation:

1. **Setup Zigbee2MQTT:**
   ```bash
   # On Raspberry Pi or Docker
   docker run -d \
     --name zigbee2mqtt \
     --device=/dev/ttyUSB0 \
     -v $(pwd)/data:/app/data \
     koenkk/zigbee2mqtt
   ```

2. **Test MQTT Connection:**
   ```bash
   mosquitto_sub -h localhost -t 'zigbee2mqtt/#' -v
   ```

3. **Create Component:**
   ```bash
   cd components
   mkdir zigbee_mqtt
   cd zigbee_mqtt
   touch zigbee_mgr.h zigbee_mgr.c zigbee_mqtt.h zigbee_mqtt.c
   ```

4. **Implement MQTT Bridge:**
   - Subscribe to Zigbee2MQTT topics
   - Parse device state messages
   - Publish control commands
   - Map devices to zones/relays

---

## Success Criteria

### Tuya Support Complete When:
- ✅ Can discover Tuya devices
- ✅ Can control switches and lights
- ✅ Can monitor sensors (trigger zones)
- ✅ Can map to virtual zones/relays
- ✅ Web UI for device management
- ✅ Configuration persists across reboots

### Zigbee Support Complete When:
- ✅ Can connect to Zigbee2MQTT
- ✅ Can discover Zigbee devices
- ✅ Can control Zigbee outputs
- ✅ Can monitor Zigbee sensors
- ✅ Can map to virtual zones/relays
- ✅ Web UI for device management

---

**Status:** 📋 PLANNING COMPLETE - Ready for Implementation

**Recommendation:** Start with **Tuya** (uses existing hardware), then add **Zigbee** (broader ecosystem). Defer **Matter** until ecosystem matures (2027+).
