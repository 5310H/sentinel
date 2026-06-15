# ESPHome Native API Test Guide

## Overview
This guide shows how to test the ESPHome Native API integration without real ESP hardware.

## Mock Devices
The test environment includes 3 simulated ESPHome devices:

1. **Basement Motion** (`basement-motion.local`)
   - Type: Binary Sensor
   - Mapped to: Zone 33
   - MAC: 24:0A:C4:12:34:56

2. **Kitchen Light** (`kitchen-light.local`)
   - Type: Switch
   - Mapped to: Relay 8
   - MAC: 24:0A:C4:78:90:AB

3. **Window Contact** (`window-contact.local`)
   - Type: Binary Sensor
   - Mapped to: Zone 34
   - MAC: 24:0A:C4:CD:EF:12

## Building & Running

```bash
cd test_linux
make clean
make
./sentinel_test
```

Server starts on:
- HTTP: http://localhost:8000
- HTTPS: https://localhost:8443

## API Endpoints

### Get ESPHome Devices
```bash
curl http://localhost:8000/api/esphome/devices
```

Response:
```json
{
  "devices": [
    {
      "hostname": "basement-motion.local",
      "mac": "24:0A:C4:12:34:56",
      "version": "2024.1.0",
      "entity_id": "basement_motion",
      "entity_name": "Basement Motion",
      "entity_key": 1001,
      "type": "binary_sensor",
      "state": false,
      "zone": 33
    },
    {
      "hostname": "kitchen-light.local",
      "mac": "24:0A:C4:78:90:AB",
      "version": "2024.1.0",
      "entity_id": "kitchen_light",
      "entity_name": "Kitchen Light",
      "entity_key": 2001,
      "type": "switch",
      "state": false,
      "relay": 8
    },
    {
      "hostname": "window-contact.local",
      "mac": "24:0A:C4:CD:EF:12",
      "version": "2024.1.0",
      "entity_id": "window_contact",
      "entity_name": "Window Contact",
      "entity_key": 3001,
      "type": "binary_sensor",
      "state": false,
      "zone": 34
    }
  ]
}
```

### Trigger Sensor
```bash
# Trip basement motion sensor
curl -X POST http://localhost:8000/api/esphome/trigger \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"basement_motion","state":true}'

# Clear sensor
curl -X POST http://localhost:8000/api/esphome/trigger \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"basement_motion","state":false}'
```

### Control Switch
```bash
# Turn on kitchen light
curl -X POST http://localhost:8000/api/esphome/control \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"kitchen_light","state":true}'

# Turn off
curl -X POST http://localhost:8000/api/esphome/control \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"kitchen_light","state":false}'
```

## Test Scenarios

### 1. Sensor Zone Trip
```bash
# Arm the system (stay mode)
curl -X POST http://localhost:8000/api/arm -d '{"mode":"stay"}'

# Trip basement motion (zone 33)
curl -X POST http://localhost:8000/api/esphome/trigger \
  -d '{"entity_id":"basement_motion","state":true}'

# Check zones
curl http://localhost:8000/api/zones
```

Expected: Zone 33 should show as tripped, alarm may trigger depending on arm mode.

### 2. Switch Control
```bash
# Turn on kitchen light (relay 8)
curl -X POST http://localhost:8000/api/esphome/control \
  -d '{"entity_id":"kitchen_light","state":true}'

# Check relay status
curl http://localhost:8000/api/relays
```

Expected: Relay 8 state should show as ON.

### 3. Multiple Sensors
```bash
# Trip window contact
curl -X POST http://localhost:8000/api/esphome/trigger \
  -d '{"entity_id":"window_contact","state":true}'

# Trip basement motion
curl -X POST http://localhost:8000/api/esphome/trigger \
  -d '{"entity_id":"basement_motion","state":true}'

# Check all zones
curl http://localhost:8000/api/zones
```

Expected: Zones 33 and 34 both show as tripped.

## Comparing with Matter

ESPHome API vs Matter:

| Feature | ESPHome API | Matter |
|---------|------------|--------|
| Device Support | All ESP chips | ESP32-C3/C6/H2 only |
| Port | TCP 6053 | UDP 5540 |
| Protocol | Binary (Protobuf) | Binary (TLV) |
| Latency | 10-20ms | 50-100ms |
| Commercial Devices | No | Yes |
| DIY Devices | Yes | Limited |

## Integration with Main System

Both ESPHome API and Matter devices map to:
- **Virtual Zones** (33-96) for sensors
- **Virtual Relays** (8-31) for switches/lights

This means:
1. Physical zones 1-32 (wired sensors on KC868-A8V3)
2. Virtual zones 33-96 (wireless sensors via ESPHome/Matter)
3. Physical relays 1-7 (wired outputs on KC868-A8V3)
4. Virtual relays 8-31 (wireless switches/lights via ESPHome/Matter)

## Real Device Configuration

To connect real ESPHome devices, add to your ESPHome YAML:

```yaml
api:
  password: "your_secure_password"
  encryption:
    key: "your_32_byte_base64_encryption_key"

# Optional: Static IP for reliability
wifi:
  manual_ip:
    static_ip: 192.168.1.100
    gateway: 192.168.1.1
    subnet: 255.255.255.0
```

Then in Sentinel-ESP, connect via hostname or IP:
```c
esphome_api_connect("192.168.1.100", 6053, "your_secure_password");
```

## Troubleshooting

1. **Connection failed**: Check firewall, port 6053
2. **Authentication failed**: Verify password matches ESPHome config
3. **No entities**: Check ESPHome device has sensors/switches configured
4. **Sensor not triggering**: Verify zone mapping (33-96 range)

## Next Steps

1. Test sensor debouncing with rapid triggers
2. Test alarm response (all lights to 100%)
3. Test zone bypass with ESPHome sensors
4. Build web UI for device management
