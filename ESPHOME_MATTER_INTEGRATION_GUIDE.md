# ESPHome & Matter Integration Guide for Sentinel-ESP

## Current State (Your System)

### What You Have Now:
- **Primary Zones**: 32 GPIO/I2C-based zones (physical wiring)
- **MQTT Integration**: Home Assistant autodiscovery working
- **Web Dashboard**: Local HTTPS control
- **W5500 Ethernet**: Stable network connection

### What's Missing:
- **No wireless zone expansion**: Need physical wiring for new zones
- **No control of other ESP devices**: Can't command ESPHome sensors/switches
- **Limited to physical relays**: Only 8 relay outputs

---

## Proposed Solution: Add Device Control

### Goal:
Enable your Sentinel-ESP to **communicate with other smart devices** as:
1. **Additional wireless zones** (motion sensors, door contacts)
2. **Additional outputs** (smart switches, lights, locks)
3. **Environmental monitoring** (temperature, leak, smoke detectors)

---

## Option 1: ESPHome Native API

### What It Is:
- ESPHome's proprietary protocol for device-to-device communication
- Binary TCP protocol on port 6053
- Used by Home Assistant to talk to ESPHome devices

### Architecture:
```
┌─────────────────┐         ESPHome Native API         ┌──────────────────┐
│  Sentinel-ESP   │◄──────────(port 6053)─────────────►│ ESPHome Motion   │
│   (Controller)  │                                     │     Sensor       │
└─────────────────┘                                     └──────────────────┘
        │
        │ ESPHome Native API
        ▼
┌──────────────────┐
│ ESPHome Smart    │
│     Switch       │
└──────────────────┘
```

### Implementation:
```c
// ACTUAL IMPLEMENTATION - Located in components/esphome_api/

// 1. Component Structure
components/esphome_api/
├── esphome_api_client.h      // Public API header
├── esphome_api_client.c      // Connection & device management (382 lines)
├── esphome_api_protocol.h    // Protocol definitions
├── esphome_api_protocol.c    // Message encoding/decoding
└── CMakeLists.txt            // Build configuration

// 2. Data Structures
typedef struct {
    char hostname[64];            // Device IP/hostname
    char friendly_name[64];       // Display name
    uint16_t port;                // API port (6053)
    bool is_connected;            // Connection state
    int socket_fd;                // TCP socket
    char encryption_key[64];      // Optional encryption
    uint32_t last_ping_ms;        // Keepalive
} esphome_device_t;

typedef struct {
    uint32_t entity_key;          // Unique entity ID
    char object_id[64];           // Entity name
    esphome_entity_type_t type;   // BINARY_SENSOR, SWITCH, etc.
    char device_hostname[64];     // Parent device
    bool current_state;           // Current state
    int8_t virtual_zone_id;       // Mapped to zone 33-96
    int8_t virtual_relay_id;      // Mapped to relay 8-31
} esphome_entity_t;

// 3. Initialization (called from main.c during startup)
// File: main/main.c line 950-954
extern int esphome_api_init(void);
if (esphome_api_init() == 0) {
    ESP_LOGI(TAG, "ESPHome API client initialized");
} else {
    ESP_LOGW(TAG, "ESPHome API client initialization failed");
}

// 4. Connect to Device
esphome_api_connect("192.168.1.50", 6053, "password", NULL);

// 5. Subscribe to Entity States
esphome_api_subscribe_states("192.168.1.50", motion_callback);

// 6. Control Entities
esphome_api_send_switch_command("192.168.1.50", entity_key, true);

// 7. Map to Virtual Zones
esphome_api_map_to_zone("192.168.1.50", entity_key, 33);  // Zone 33
```

**How It's Loaded:**

1. **Configuration File: esphome.json**
   ```json
   // File: data/esphome.json (loaded from SD card or SPIFFS)
   [
     {
       "hostname": "192.168.1.100",
       "port": 6053,
       "friendly_name": "Garage Motion Sensor",
       "password": "",
       "encryption_key": "",
       "virtual_zone_start": 33,
       "virtual_relay_start": 8,
       "enabled": true
     },
     {
       "hostname": "192.168.1.101",
       "port": 6053,
       "friendly_name": "Front Door Contact",
       "password": "my_api_password",
       "encryption_key": "",
       "virtual_zone_start": 34,
       "virtual_relay_start": 9,
       "enabled": true
     }
   ]
   ```

2. **Storage Manager Integration**
   ```c
   // File: components/storage/storage_mgr.h
   typedef struct {
       char hostname[STR_SMALL];          // IP or hostname
       uint16_t port;                     // API port (6053)
       char friendly_name[STR_SMALL];     // Display name
       char password[STR_SMALL];          // API password
       char encryption_key[STR_MEDIUM];   // Encryption key
       int8_t virtual_zone_start;         // First zone (33-96)
       int8_t virtual_relay_start;        // First relay (8-31)
       bool enabled;                      // Enable/disable
   } esphome_device_t;

   extern esphome_device_t esphome_devices[32];
   extern int esphome_count;
   ```

   ```c
   // File: components/storage/storage_mgr.c
   // Loaded in storage_load_all() - line 687
   if ((data = read_file("esphome.json"))) {
       root = cJSON_Parse(data);
       cJSON_ArrayForEach(item, root) {
           safe_strncpy(esphome_devices[esphome_count].hostname, ...);
           esphome_devices[esphome_count].port = ...;
           esphome_devices[esphome_count].enabled = ...;
           esphome_count++;
       }
   }
   ```

3. **Build System Integration**
   ```cmake
   # components/esphome_api/CMakeLists.txt
   idf_component_register(
       SRCS "esphome_api_client.c" "esphome_api_protocol.c"
       INCLUDE_DIRS "."
       REQUIRES lwip nvs_flash engine storage comms
   )
   ```
   - ESP-IDF automatically discovers components in `components/` folder
   - Links with lwip (TCP/IP stack), FreeRTOS, and storage manager

4. **Startup Sequence (main.c line 940-960)**
   ```
   1. NVS Init
   2. Network Init (W5500 Ethernet)
   3. Storage Init → storage_load_all()
      → Loads config.json, zones.json, relays.json, esphome.json
   4. Certificate Manager Init
   5. Engine Init
   6. → ESPHome API Init ← (line 950)
      → Reads esphome_devices[] array
      → Auto-connects to all enabled devices
   7. Matter Controller Init (if enabled)
   8. OLED Display Init
   9. Web Server Start (Mongoose)
   10. Monitor Task Start
   ```

3. **Runtime Operation**
   ```
   ┌─────────────────────────────────────────┐
   │ ESP32-S3 Sentinel Device                │
   │                                         │
   │  [main.c] → esphome_api_init()         │
   │       ↓                                 │
   │  [esphome_api_client.c]                │
   │       • Creates mutexes                 │
   │       • Allocates device registry       │
   │       • Initializes protocol layer      │
   │       ↓                                 │
   │  Ready to connect to ESPHome devices   │
   │                                         │
   │  User calls:                            │
   │  esphome_api_connect("192.168.1.50")   │
   │       ↓                                 │
   │  [lwip] TCP socket to port 6053        │
   │       ↓                                 │
   │  [esphome_api_protocol.c]              │
   │       • Sends HELLO message             │
   │       • Receives device info            │
   │       • Subscribes to entity states     │
   │       ↓                                 │
   │  Stores entities in g_entities[]       │
   │  Maps to virtual zones (33-96)         │
   │                                         │
   │  When state changes:                    │
   │  ESPHome → TCP → Protocol decoder       │
   │             → Callback                  │
   │             → engine_trigger_zone(33)   │
   └─────────────────────────────────────────┘
   ```

4. **Memory Footprint**
   - Code size: ~45KB (client + protocol)
   - RAM usage: ~8KB static (device/entity registries)
   - Per connection: ~4KB (TCP buffers)
   - Total: ~50-60KB with 5 devices

5. **Component Dependencies**
   ```
   esphome_api
   ├── lwip (TCP/IP sockets)
   ├── nvs_flash (persist config)
   ├── freertos (tasks, mutexes)
   ├── engine (trigger zones)
   └── storage (virtual zone mapping)
   ```

### Pros:
- ✅ Direct device-to-device (no broker)
- ✅ Low latency (~10-20ms)
- ✅ Encrypted connection
- ✅ Real-time push notifications
- ✅ Simpler than Matter
- ✅ Smaller code footprint (~50KB)

### Cons:
- ❌ Only works with ESPHome devices
- ❌ Proprietary protocol (not industry standard)
- ❌ Can't control commercial Matter devices
- ❌ Need to implement protocol client from scratch

### Device Compatibility:
**Works With:**
- ESPHome motion sensors
- ESPHome door/window sensors
- ESPHome switches/relays
- ESPHome temperature sensors
- Any DIY ESP device running ESPHome firmware

**Doesn't Work With:**
- Philips Hue bulbs
- Aqara sensors
- Commercial smart locks
- Non-ESPHome devices

---

## Option 2: Matter Protocol

### What It Is:
- Industry standard protocol (Apple, Google, Amazon, Samsung)
- Unified smart home protocol replacing Zigbee/Z-Wave fragmentation
- Works over WiFi (no border router needed for WiFi devices)

### Architecture:
```
┌─────────────────┐         Matter Protocol           ┌──────────────────┐
│  Sentinel-ESP   │◄──────────(UDP 5540)──────────────►│ ESPHome Motion   │
│ (Matter         │                                     │  (Matter Device) │
│  Controller)    │                                     └──────────────────┘
└─────────────────┘
        │                                               ┌──────────────────┐
        │ Matter Protocol                               │ Philips Hue Bulb │
        ├──────────────────────────────────────────────►│  (Matter Device) │
        │                                               └──────────────────┘
        │                                               ┌──────────────────┐
        │                                               │ Aqara Door       │
        └──────────────────────────────────────────────►│    Sensor        │
                                                        └──────────────────┘
```

### Multi-Controller Support:
```
        ┌──────────────────┐
        │  Matter Device   │
        │  (Smart Lock)    │
        └────────┬─────────┘
                 │
         Controlled by ALL:
                 │
    ┌────────────┼────────────┐
    │            │            │
┌───▼─────┐  ┌──▼───────┐  ┌─▼──────────┐
│Sentinel │  │   Home   │  │   Apple    │
│  ESP    │  │Assistant │  │   HomeKit  │
└─────────┘  └──────────┘  └────────────┘
```

### Implementation:
```c
// Pseudocode - What we'd implement
#include "esp_matter.h"
#include "esp_matter_controller.h"

// Initialize Matter controller
esp_matter_controller_init();

// Commission device (one-time pairing)
esp_matter_commission_device("setup_code_12345678");

// Read occupancy sensor
bool motion = matter_read_occupancy_sensor(node_id, endpoint_id);

// Control on/off switch
matter_send_on_off_command(node_id, endpoint_id, true);
```

### Pros:
- ✅ Industry standard (future-proof)
- ✅ Works with ESPHome **and** commercial devices
- ✅ Multi-controller (shared with HA, Apple, Google)
- ✅ Standardized device types (guaranteed compatibility)
- ✅ ESP-IDF has built-in `esp_matter` support
- ✅ Local control (no cloud)
- ✅ Encrypted and secure

### Cons:
- ❌ More complex implementation (~200KB code)
- ❌ Higher memory usage
- ❌ Requires commissioning/pairing process
- ❌ Slightly higher latency (~20-50ms)
- ❌ Still maturing (some edge cases)

### Device Compatibility:
**Works With:**
- ESPHome devices (with Matter support)
- Philips Hue bulbs/strips
- Aqara sensors (motion, door, temperature)
- Eve smart home devices
- Nanoleaf lights
- Yale/Schlage smart locks
- Any Matter-certified device (1000+ products in 2026)

**Doesn't Work With:**
- Zigbee-only devices (without Matter bridge)
- Z-Wave devices (without Matter bridge)
- Proprietary WiFi devices (Tuya, etc.)

---

## Option 3: MQTT (What You Already Have)

### Architecture:
```
                    ┌──────────────┐
                    │ MQTT Broker  │
                    │(Home Assist) │
                    └──────┬───────┘
                           │
            ┏━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━┓
            ▼                              ▼
    ┌──────────────┐              ┌──────────────┐
    │ Sentinel-ESP │              │   ESPHome    │
    │ (Subscribe)  │              │   Device     │
    └──────────────┘              └──────────────┘
```

### Implementation:
```c
// Already implemented in mqtt_logic.c
// Just need to add subscriptions

// Subscribe to ESPHome device topics
mg_mqtt_sub(c, "esphome/motion_sensor/state");
mg_mqtt_sub(c, "esphome/door_sensor/state");

// Publish commands
mqtt_publish("esphome/switch/relay/command", "ON");
```

### Pros:
- ✅ Already implemented in your system
- ✅ Works with any MQTT device
- ✅ Simple to add new subscriptions
- ✅ Proven, reliable technology
- ✅ Easy debugging (MQTT Explorer)

### Cons:
- ❌ Requires MQTT broker (Home Assistant or mosquitto)
- ❌ Higher latency (~50-200ms)
- ❌ Text-based protocol (more bandwidth)
- ❌ Single point of failure (broker)
- ❌ Manual topic configuration

---

## Comparison Matrix

| Feature | ESPHome Native API | Matter | MQTT (Current) |
|---------|-------------------|--------|----------------|
| **Latency** | 10-20ms | 20-50ms | 50-200ms |
| **Code Size** | ~50KB | ~200KB | 0 (done) |
| **Implementation Effort** | Medium | High | Low |
| **ESPHome Devices** | ✅ | ✅ | ✅ |
| **Commercial Devices** | ❌ | ✅ | Limited |
| **No Broker Required** | ✅ | ✅ | ❌ |
| **Industry Standard** | ❌ | ✅ | ✅ |
| **Multi-Controller** | ❌ | ✅ | ❌ |
| **Memory Usage** | Low | Medium | Very Low |
| **Future-Proof** | Medium | High | Medium |

---

## Use Case Examples

### Example 1: Wireless Zone Expansion

#### With ESPHome Native API:
```yaml
# ESPHome device: garage_motion.yaml
esphome:
  name: garage-motion
  
api:
  password: "your_password"
  
binary_sensor:
  - platform: gpio
    pin: GPIO4
    name: "Garage Motion"
    device_class: motion
```

```c
// Sentinel-ESP code
void setup_virtual_zones() {
    // Connect to ESPHome device
    esphome_client_t *garage = esphome_connect("192.168.1.50", 6053, "your_password");
    
    // Map to virtual zone 33
    esphome_subscribe_binary_sensor(garage, "Garage Motion", virtual_zone_33_callback);
}

void virtual_zone_33_callback(bool state) {
    // Treat as regular zone
    if (state && engine_get_arm_state() == ARMED) {
        trigger_alarm(33, "Garage Motion Detected");
    }
}
```

#### With Matter:
```yaml
# ESPHome device: garage_motion.yaml (Matter-enabled)
esphome:
  name: garage-motion

esp32_ble_tracker:

esp32_matter:
  enable: true
  
binary_sensor:
  - platform: gpio
    pin: GPIO4
    name: "Garage Motion"
    device_class: motion
```

```c
// Sentinel-ESP code (Matter)
void setup_virtual_zones() {
    // Commission Matter device (one-time pairing)
    matter_commission_device("MT:12345678");
    
    // Subscribe to occupancy cluster
    matter_subscribe_occupancy(node_id, endpoint_1, virtual_zone_33_callback);
}

void virtual_zone_33_callback(bool occupied) {
    if (occupied && engine_get_arm_state() == ARMED) {
        trigger_alarm(33, "Garage Motion Detected");
    }
}
```

#### With MQTT:
```yaml
# ESPHome device: garage_motion.yaml
esphome:
  name: garage-motion
  
mqtt:
  broker: 192.168.1.100
  
binary_sensor:
  - platform: gpio
    pin: GPIO4
    name: "Garage Motion"
    device_class: motion
```

```c
// Sentinel-ESP code (already implemented!)
void mqtt_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_MQTT_OPEN) {
        // Subscribe to ESPHome topics
        mg_mqtt_sub(c, "esphome/garage-motion/state");
    }
    else if (ev == MG_EV_MQTT_MSG) {
        struct mg_mqtt_message *mm = ev_data;
        if (strstr(mm->topic.buf, "garage-motion")) {
            // Parse JSON: {"state": "ON"}
            // Trigger virtual zone 33
        }
    }
}
```

### Example 2: Smart Lighting Response

When alarm triggered, turn on all lights:

#### With Matter:
```c
void alarm_triggered_handler() {
    // Turn on all Matter lights
    matter_send_on_off_command(hue_bulb_1, 1, true);
    matter_send_on_off_command(hue_bulb_2, 1, true);
    matter_send_on_off_command(nanoleaf_panel, 1, true);
    matter_set_brightness(hue_bulb_1, 1, 255);  // Max brightness
}
```

#### With MQTT:
```c
void alarm_triggered_handler() {
    mqtt_publish("esphome/light1/command", "ON");
    mqtt_publish("esphome/light2/command", "ON");
    // Can't control Philips Hue directly - need bridge
}
```

---

## Recommendation

### For DIY ESPHome-Only Setup:
**Choose:** MQTT (simplest, already works)
- Just add subscriptions to ESPHome topics
- Minimal code changes
- Proven reliability

### For Mixed Ecosystem (ESPHome + Commercial):
**Choose:** Matter
- Control both DIY and commercial devices
- Future-proof standard
- Worth the implementation effort

### For Maximum Simplicity:
**Choose:** ESPHome Native API
- Best performance for ESPHome devices
- Middle ground between MQTT and Matter

---

## Testing ESPHome Integration

### Web-Based Testing Tool

The system includes a comprehensive testing interface at `https://sentinel.local/tester.html`.

#### ESPHome Device Testing Features:

**1. Add ESPHome Device**
- IP address input (e.g., `192.168.1.100`)
- Port configuration (default: `6053`)
- Optional API password support
- One-click connection

**2. Connected Device Management**
- Real-time device status display
- Entity count per device
- Remove device from list
- Connection state monitoring

**3. Automated Testing**
```
Test Suite includes:
✓ Connectivity Test - Verifies device responds
✓ Entity Discovery - Lists all sensors/switches
✓ State Subscription - Validates real-time updates
✓ Uptime & Version - Reports device information
```

**4. Test Execution**
```javascript
// Individual device test
Click "Test All ESPHome Devices"

// Full system test (includes ESPHome)
Click "Run Full System Test" → Section 11: ESPHome Integration
```

#### Console Output Example:
```
[14:23:45] ESPHome: Connecting to 192.168.1.100:6053...
[14:23:46] ESPHome: Connected to Motion Sensor Living Room
[14:23:46]   ✓ Device connected
[14:23:46]   • Uptime: 123456s
[14:23:46]   • Version: 2024.1.0
[14:23:46]   ✓ Found 3 entities
[14:23:46]   • Binary Sensors: motion, door_contact
[14:23:46]   • Switches: relay1
[14:23:46]   ✓ State subscription active
```

#### API Endpoints (To Be Implemented):

The tester.html interface expects these backend endpoints:

```c
// POST /api/esphome/connect
// Body: {"host": "192.168.1.100", "port": 6053, "password": ""}
// Response: {"status": "ok", "device_name": "...", "entities": 3}

// POST /api/esphome/status  
// Body: {"host": "192.168.1.100", "port": 6053}
// Response: {"connected": true, "uptime": "123456", "version": "2024.1.0"}

// POST /api/esphome/entities
// Body: {"host": "192.168.1.100", "port": 6053}
// Response: {"entities": [{"type": "binary_sensor", "name": "motion"}, ...]}

// POST /api/esphome/subscribe
// Body: {"host": "192.168.1.100", "port": 6053}
// Response: {"status": "ok"}
```

#### Integration with Existing Tests:

The full system test (Section 11) automatically validates all configured ESPHome devices:
- Runs connectivity checks
- Verifies each device is reachable
- Reports pass/fail in test summary
- Includes results in overall test metrics

**Test Summary includes:**
```
Passed: 45
Failed: 0
Warnings: 2
Section 11: ESPHome Integration - 3 devices tested
```

---

## Implementation Roadmap

### Phase 1: MQTT Enhancement (1-2 hours)
```
1. Add ESPHome device topic subscriptions
2. Create virtual zone mapping (33-64)
3. Add output device commands
4. Test with ESPHome motion sensor
```

### Phase 2: ESPHome Native API (1-2 days)
```
1. Integrate ESPHome API protocol library
2. Implement connection management
3. Add state subscription system
4. Create device discovery
5. Test with multiple devices
```

### Phase 3: Matter Controller (3-5 days)
```
1. Enable ESP-IDF Matter component
2. Implement commissioning flow
3. Add cluster support (On/Off, Occupancy, etc.)
4. Create device binding system
5. Web UI for device pairing
6. Test with commercial Matter devices
```

---

## Questions to Consider

1. **Do you only use ESPHome devices?**
   - Yes → MQTT or ESPHome Native API
   - No → Matter

2. **Do you need <20ms latency?**
   - Yes → ESPHome Native API or Matter
   - No → MQTT is fine

3. **Do you want commercial device support?**
   - Yes → Matter
   - No → MQTT or ESPHome Native API

4. **How much development time available?**
   - Minimal → MQTT (enhance current)
   - Moderate → ESPHome Native API
   - Significant → Matter (best long-term)

5. **Memory constraints?**
   - Tight → MQTT (0 KB added)
   - Moderate → ESPHome API (~50KB)
   - Plenty → Matter (~200KB)

---

## My Professional Recommendation

**Start with MQTT**, then migrate to Matter later:

### Phase 1: MQTT (Now)
- Enhance your existing MQTT to support ESPHome devices
- Get virtual zones working quickly
- Prove the concept with real devices

### Phase 2: Matter (Later)
- Once MQTT works, implement Matter alongside it
- Gradually migrate devices to Matter
- Keep MQTT as fallback

### Why This Approach:
- ✅ Immediate results (MQTT working today)
- ✅ Low risk (incremental changes)
- ✅ Future-proof path (Matter ready)
- ✅ No vendor lock-in (both protocols available)

---

## Next Steps

Choose one:

1. **"Enhance MQTT"** - I'll add ESPHome device subscriptions (30 minutes)
2. **"Implement ESPHome Native API"** - Full protocol client (2 days)
3. **"Implement Matter"** - Full Matter controller (5 days)
4. **"Show me code examples"** - I'll create detailed implementation previews

What would you like to do?
