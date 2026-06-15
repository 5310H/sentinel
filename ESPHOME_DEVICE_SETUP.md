# ESPHome Device Setup Guide
**Sentinel-ESP ESPHome Integration Configuration**

---

## Overview

The `esphome.json` file defines all ESPHome devices that integrate with your Sentinel alarm system. Devices are loaded automatically on boot and can be managed through the web UI at `https://sentinel.local/devices.html` or via REST API.

---

## File Location

- **ESP32 Internal Storage:** `/spiffs/esphome.json` (SPIFFS partition)
- **SD Card (optional):** `/sd/esphome.json` (synced to internal storage)
- **Development:** `data/esphome.json` (uploaded with `idf.py storage-flash`)

---

## JSON Schema

```json
[
  {
    "hostname": "string (IP or mDNS name)",
    "port": number (1-65535, default 6053),
    "friendly_name": "string (required)",
    "password": "string (optional)",
    "encryption_key": "string (base64, optional)",
    "virtual_zone_start": number (1-96),
    "virtual_relay_start": number (1-31),
    "enabled": boolean
  }
]
```

### Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `hostname` | string | ✅ Yes | IP address or mDNS hostname (e.g., `192.168.1.100` or `garage.local`) |
| `port` | number | No | Native API port (default: 6053) |
| `friendly_name` | string | ✅ Yes | Display name for web UI and logs (max 64 chars) |
| `password` | string | No | Native API password (if configured in ESPHome) |
| `encryption_key` | string | No | Base64-encoded 32-byte encryption key |
| `virtual_zone_start` | number | No | First zone number for sensors (default: 33, range: 1-96) |
| `virtual_relay_start` | number | No | First relay number for switches (default: 8, range: 1-31) |
| `enabled` | boolean | No | Enable/disable device (default: true) |

---

## Configuration Examples

### Example 1: Basic Motion Sensor (No Security)
```json
{
  "hostname": "192.168.1.100",
  "port": 6053,
  "friendly_name": "Garage Motion Sensor",
  "password": "",
  "encryption_key": "",
  "virtual_zone_start": 33,
  "virtual_relay_start": 8,
  "enabled": true
}
```

**Use Case:** Simple PIR motion sensor in garage  
**Entities:** 1 binary sensor → Zone 33  
**Security:** None (local network only)

---

### Example 2: Door Contact with Password
```json
{
  "hostname": "front-door.local",
  "port": 6053,
  "friendly_name": "Front Door Contact",
  "password": "my_secure_password",
  "encryption_key": "",
  "virtual_zone_start": 34,
  "virtual_relay_start": 9,
  "enabled": true
}
```

**Use Case:** Front door contact sensor  
**Entities:** 1 binary sensor → Zone 34  
**Security:** Password-protected API  
**ESPHome Config:**
```yaml
api:
  password: "my_secure_password"
```

---

### Example 3: Multi-Sensor with Encryption
```json
{
  "hostname": "192.168.1.102",
  "port": 6053,
  "friendly_name": "Living Room Multi-Sensor",
  "password": "",
  "encryption_key": "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY=",
  "virtual_zone_start": 35,
  "virtual_relay_start": 10,
  "enabled": true
}
```

**Use Case:** Room with multiple sensors  
**Entities:**
- Motion sensor → Zone 35
- Window contact → Zone 36
- Door contact → Zone 37
- Smart light switch → Relay 10

**Security:** Encrypted API (recommended for remote access)  
**ESPHome Config:**
```yaml
api:
  encryption:
    key: "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY="
```

---

### Example 4: Smart Switch with Multiple Outputs
```json
{
  "hostname": "basement-hub.local",
  "port": 6053,
  "friendly_name": "Basement Smart Hub",
  "password": "hub_password",
  "encryption_key": "",
  "virtual_zone_start": 40,
  "virtual_relay_start": 15,
  "enabled": true
}
```

**Use Case:** Central hub with multiple switches/lights  
**Entities:**
- Flood sensor → Zone 40
- 4x Light switches → Relays 15-18
- Siren output → Relay 19

---

### Example 5: Disabled Device (Testing/Maintenance)
```json
{
  "hostname": "192.168.1.105",
  "port": 6053,
  "friendly_name": "Outdoor Camera (Offline)",
  "password": "",
  "encryption_key": "",
  "virtual_zone_start": 50,
  "virtual_relay_start": 25,
  "enabled": false
}
```

**Use Case:** Temporarily disable device without deleting configuration

---

## Virtual Zone/Relay Mapping

### Zone Allocation (Total: 96 Zones)
- **Zones 1-32:** Physical hardwired sensors (RF keypads, wired contacts)
- **Zones 33-64:** ESPHome devices (recommended allocation)
- **Zones 65-96:** Tuya devices (ESPHome uses 33-64)

### Relay Allocation (Total: 31 Relays)
- **Relays 1-7:** Physical hardwired outputs (sirens, strobes, locks)
- **Relays 8-23:** ESPHome smart switches/lights (recommended allocation)
- **Relays 24-31:** ESPHome expansion devices

### Zone/Relay Calculation Example

**Device:** Living Room Multi-Sensor with 3 sensors, 2 switches

```json
{
  "virtual_zone_start": 35,    // First sensor at zone 35
  "virtual_relay_start": 10    // First switch at relay 10
}
```

**Mapping:**
- Motion sensor → Zone 35
- Window 1 → Zone 36
- Window 2 → Zone 37
- Light switch → Relay 10
- Fan switch → Relay 11

**Next Device:**
```json
{
  "virtual_zone_start": 38,    // Start after 35, 36, 37
  "virtual_relay_start": 12    // Start after 10, 11
}
```

---

## Security Recommendations

### Level 1: Local Network Only (No Security)
```json
{
  "password": "",
  "encryption_key": ""
}
```
✅ **Use When:** Device on isolated VLAN, no internet exposure  
⚠️ **Risk:** Anyone on network can access API

---

### Level 2: Password Protection
```json
{
  "password": "strong_password_here",
  "encryption_key": ""
}
```
✅ **Use When:** Trusted local network, basic authentication needed  
⚠️ **Risk:** Password sent in plaintext (network sniffing possible)

**Generate Strong Password:**
```bash
openssl rand -base64 16
```

---

### Level 3: Encrypted API (Recommended)
```json
{
  "password": "",
  "encryption_key": "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY="
}
```
✅ **Use When:** Production deployment, internet-exposed networks  
✅ **Benefits:** All API traffic encrypted with AES

**Generate Encryption Key:**
```bash
# On Linux/Mac:
openssl rand -base64 32

# On ESPHome device (in YAML):
api:
  encryption:
    key: !secret api_encryption_key  # Auto-generates if not set
```

Copy the generated key to both ESPHome YAML and esphome.json.

---

### Level 4: Password + Encryption (Maximum Security)
```json
{
  "password": "strong_password_here",
  "encryption_key": "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY="
}
```
✅ **Use When:** High-security environments, remote access  
✅ **Benefits:** Double authentication layer + encryption

---

## ESPHome Device Configuration

### Basic Sensor Example
```yaml
# File: garage-motion.yaml
esphome:
  name: garage-motion
  platform: ESP8266
  board: nodemcuv2

wifi:
  ssid: "YourNetwork"
  password: "YourWiFiPassword"

api:
  password: "my_api_password"  # Must match esphome.json

binary_sensor:
  - platform: gpio
    pin: D1
    name: "Garage Motion"
    device_class: motion
```

### Encrypted Device Example
```yaml
# File: living-room-hub.yaml
esphome:
  name: living-room-hub
  platform: ESP32
  board: nodemcu-32s

wifi:
  ssid: "YourNetwork"
  password: "YourWiFiPassword"

api:
  encryption:
    key: "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY="  # Must match esphome.json

binary_sensor:
  - platform: gpio
    pin: GPIO12
    name: "Living Room Motion"
    device_class: motion
  
  - platform: gpio
    pin: GPIO13
    name: "Living Room Window"
    device_class: window

switch:
  - platform: gpio
    pin: GPIO14
    name: "Living Room Light"
```

---

## Management Methods

### Method 1: Web UI (Recommended for Users)
1. Navigate to `https://sentinel.local/devices.html`
2. Login with admin credentials
3. Click "Add ESPHome Device"
4. Fill in form fields
5. Click "Add Device"

**Features:**
- ✅ Form validation
- ✅ Real-time status
- ✅ Edit/Delete capabilities
- ✅ No file editing required

---

### Method 2: REST API (Recommended for Automation)

**List Devices:**
```bash
curl https://sentinel.local/api/esphome
```

**Add Device:**
```bash
curl -X POST https://sentinel.local/api/esphome/add \
  -H "Content-Type: application/json" \
  -d '{
    "hostname": "192.168.1.100",
    "port": 6053,
    "friendly_name": "New Device",
    "password": "",
    "encryption_key": "",
    "virtual_zone_start": 45,
    "virtual_relay_start": 20,
    "enabled": true
  }'
```

**Update Device:**
```bash
curl -X POST https://sentinel.local/api/esphome/update \
  -H "Content-Type: application/json" \
  -d '{
    "hostname": "192.168.1.100",
    "friendly_name": "Updated Name",
    "enabled": false
  }'
```

**Delete Device:**
```bash
curl -X POST https://sentinel.local/api/esphome/delete \
  -H "Content-Type: application/json" \
  -d '{"hostname": "192.168.1.100"}'
```

---

### Method 3: Direct File Edit (Recommended for Initial Setup)

1. **Edit File:**
   ```bash
   nano data/esphome.json
   ```

2. **Upload to ESP32:**
   ```bash
   cd /home/kjgerhart/sentinel-esp
   idf.py storage-flash
   ```

3. **Reboot Device:**
   - Via web UI: Settings → System → Reboot
   - Via API: `curl -X POST https://sentinel.local/api/reboot`
   - Physical: Power cycle

---

## Troubleshooting

### Device Not Connecting
**Symptom:** Device shows in list but status = "disconnected"

**Checks:**
1. Verify hostname/IP is correct: `ping 192.168.1.100`
2. Check port is open: `nc -zv 192.168.1.100 6053`
3. Verify ESPHome API is enabled in YAML
4. Check password/encryption key matches exactly
5. Review logs: `idf.py monitor | grep ESPHOME`

**Common Fixes:**
- Wrong encryption key → Update esphome.json or ESPHome YAML
- Firewall blocking port 6053 → Allow TCP/6053 inbound
- mDNS not working → Use IP address instead of .local

---

### Entities Not Appearing as Zones/Relays
**Symptom:** Device connected but sensors/switches not working

**Checks:**
1. Verify entity is published: ESPHome web dashboard → Entities
2. Check virtual_zone_start/virtual_relay_start don't overlap with other devices
3. Ensure zone/relay numbers are within valid range (1-96 zones, 1-31 relays)

**Fix:**
```json
// Before (overlapping):
{"virtual_zone_start": 33}  // Device 1
{"virtual_zone_start": 33}  // Device 2 (CONFLICT!)

// After (fixed):
{"virtual_zone_start": 33}  // Device 1 uses zones 33-40
{"virtual_zone_start": 41}  // Device 2 starts at 41
```

---

### Encryption Key Mismatch
**Symptom:** "Failed to connect" or "Authentication failed"

**Fix:**
1. Get key from ESPHome logs during first compile
2. Copy **exact** base64 string (64 characters)
3. Update both ESPHome YAML and esphome.json
4. Recompile ESPHome device
5. Reboot Sentinel-ESP

---

### Device Limit Reached
**Symptom:** "Device limit reached (32)"

**Fix:**
- Maximum 32 ESPHome devices supported
- Delete unused devices to free slots
- Consider consolidating multiple sensors onto single ESP32 hubs

---

## Performance Considerations

### Memory Usage
- Each device: ~200 bytes RAM
- 32 devices: ~6.4KB RAM
- Entities: ~150 bytes per sensor/switch

### Network Traffic
- Idle: ~50 bytes/sec per device (keepalive)
- Active: ~200 bytes per state change
- Reconnect: ~2KB handshake

### Recommended Limits
- **Devices:** 10-20 for optimal performance
- **Entities per device:** 10-15 sensors/switches
- **Total zones used:** Keep under 80 for UI responsiveness

---

## Migration from Manual Configuration

If you previously used hardcoded devices in `esphome_api_client.c`:

1. **Extract Current Config:**
   ```c
   // Old code (before):
   {"192.168.1.100", 6053, "Garage", "", "", 33, 8, true}
   ```

2. **Convert to JSON:**
   ```json
   {
     "hostname": "192.168.1.100",
     "port": 6053,
     "friendly_name": "Garage",
     "password": "",
     "encryption_key": "",
     "virtual_zone_start": 33,
     "virtual_relay_start": 8,
     "enabled": true
   }
   ```

3. **Remove Hardcoded Arrays:**
   - Delete static device arrays from source code
   - Use storage_mgr global arrays instead

4. **Upload Configuration:**
   ```bash
   idf.py storage-flash
   ```

---

## Complete Example Configuration

```json
[
  {
    "hostname": "192.168.1.100",
    "port": 6053,
    "friendly_name": "Garage Motion + Door",
    "password": "",
    "encryption_key": "Wvl3pBPhpaQsjq2JB87xXFLWvHh/BmP5Vu7fkGw8xXY=",
    "virtual_zone_start": 33,
    "virtual_relay_start": 8,
    "enabled": true
  },
  {
    "hostname": "front-door.local",
    "port": 6053,
    "friendly_name": "Front Entryway",
    "password": "secure_pass",
    "encryption_key": "",
    "virtual_zone_start": 35,
    "virtual_relay_start": 9,
    "enabled": true
  },
  {
    "hostname": "192.168.1.102",
    "port": 6053,
    "friendly_name": "Living Room Hub",
    "password": "",
    "encryption_key": "AnotherKeyHere1234567890ABCDEFGH==",
    "virtual_zone_start": 37,
    "virtual_relay_start": 10,
    "enabled": true
  },
  {
    "hostname": "basement.local",
    "port": 6053,
    "friendly_name": "Basement Multi-Sensor",
    "password": "basement123",
    "encryption_key": "",
    "virtual_zone_start": 40,
    "virtual_relay_start": 15,
    "enabled": true
  },
  {
    "hostname": "192.168.1.105",
    "port": 6053,
    "friendly_name": "Backyard Motion (Testing)",
    "password": "",
    "encryption_key": "",
    "virtual_zone_start": 50,
    "virtual_relay_start": 25,
    "enabled": false
  }
]
```

**Total Allocation:**
- 5 devices configured
- Zones used: 33-50 (17 zones)
- Relays used: 8-25 (17 relays)
- Available zones: 51-96 (45 zones)
- Available relays: 26-31 (5 relays)

---

## References

- **ESPHome Documentation:** https://esphome.io/components/api.html
- **Encryption Setup:** https://esphome.io/components/api.html#api-encryption
- **Web UI:** https://sentinel.local/devices.html
- **API Docs:** See `ESPHOME_MATTER_INTEGRATION_GUIDE.md` (contains ESPHome API details)
- **Security Review:** See `SECURITY_REVIEW_2026-01-30.md`

---

## Quick Start Checklist

- [ ] Install ESPHome on device (esp8266/esp32)
- [ ] Configure WiFi and API in YAML
- [ ] Generate encryption key (optional but recommended)
- [ ] Add device to esphome.json
- [ ] Upload esphome.json to ESP32: `idf.py storage-flash`
- [ ] Reboot Sentinel-ESP
- [ ] Verify connection in web UI: https://sentinel.local/devices.html
- [ ] Test sensor triggers and relay controls
- [ ] Configure zone names and alarm settings

---

**Last Updated:** 2026-01-30  
**Version:** 1.0  
**Compatibility:** Sentinel-ESP v2.0+, ESPHome 2024.x+
