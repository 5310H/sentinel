# ESPHome & Tuya Device Configuration

## ESPHome Devices (esphome.json)

Each ESPHome device can create virtual zones (sensors) and/or virtual relays (switches).

**Fields:**
- `hostname`: Device hostname (e.g., "device.local")
- `port`: API port (default: 6053)
- `friendly_name`: Display name in UI
- `description`: Detailed description
- `location`: Physical location
- `password`: ESPHome API password (optional)
- `encryption_key`: Base64 encryption key (optional)
- `virtual_zone_start`: Zone ID (33-64 for ESPHome, 0 = none)
- `virtual_relay_start`: Relay ID (9-32 for ESPHome, 0 = none)
- `enabled`: true/false

**Zone Ranges:**
- Physical Zones: 1-32
- ESPHome Zones: 33-64
- Tuya Zones: 65-96

**Relay Ranges:**
- Physical Relays: 1-8
- ESPHome Relays: 9-32
- Tuya Relays: 33-64

## Tuya Devices (tuya.json)

Tuya devices are auto-discovered via the Tuya Cloud API. This file stores their configuration.

**Fields:**
- `device_id`: Tuya device ID (auto-assigned)
- `name`: Display name
- `description`: Device description
- `location`: Physical location
- `type`: Device type (1=switch, 2=light, 4=door sensor, 5=motion sensor)
- `virtual_zone_id`: Zone ID (65-96 for Tuya, -1 = none)
- `virtual_relay_id`: Relay ID (33-64 for Tuya, -1 = none)
- `enabled`: true/false

## Example Configuration

See `esphome.json.example` and `tuya.json.example` for templates.

## Notes

1. Zone IDs must be unique across all devices
2. Relay IDs must be unique across all devices
3. Set zone/relay IDs to 0 or -1 if the device doesn't need that capability
4. Virtual zones work like physical zones in the alarm engine
5. Virtual relays can be controlled like physical relays
