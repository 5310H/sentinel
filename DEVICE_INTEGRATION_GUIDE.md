# ESPHome & Matter Device Integration Guide

Complete guide for connecting ESPHome and Matter devices to your Sentinel-ESP alarm system.

---

## Table of Contents
1. [Overview](#overview)
2. [ESPHome Device Setup](#esphome-device-setup)
3. [Matter Device Setup](#matter-device-setup)
4. [Connecting to Sentinel-ESP](#connecting-to-sentinel-esp)
5. [Zone and Relay Mapping](#zone-and-relay-mapping)
6. [Testing](#testing)
7. [Troubleshooting](#troubleshooting)

---

## Overview

### What You Can Do

**Wireless Sensors (Virtual Zones 33-96):**
- Motion sensors → Trigger alarm when armed
- Door/window contacts → Monitor entry points
- Glass break sensors → Perimeter protection
- Water leak detectors → Environmental monitoring
- Smoke/CO detectors → Emergency alerts

**Smart Outputs (Virtual Relays 8-31):**
- Lights → Auto-on during alarm
- Smart locks → Auto-lock when armed
- Garage doors → Emergency control
- Sirens → Additional alarm sounders
- Cameras → Start recording on trigger

### Three Integration Methods

| Method | Works With | Difficulty | Best For | Status |
|--------|-----------|------------|----------|--------|
| **ESPHome API** | All ESPHome devices | Easy | Existing ESPHome setups | ✅ Active |
| **Matter** | ESP32-C3/C6/H2 + commercial | Medium | New devices, future-proof | ⚠️ Optional* |
| **MQTT** | Everything | Easy | Simple setups, legacy devices | ✅ Active |

\* Matter requires ESP-Matter SDK installation. See [MATTER_OPTIONAL_SETUP.md](MATTER_OPTIONAL_SETUP.md) for details.

---

## ESPHome Device Setup

### Prerequisites
- ESPHome installed (https://esphome.io)
- ESP32, ESP8266, or ESP32-C3/C6/H2
- Home Assistant (optional)

### Basic Motion Sensor Example

**File:** `garage_motion.yaml`

```yaml
esphome:
  name: garage-motion
  friendly_name: "Garage Motion Sensor"

# Choose your board
esp32:
  board: esp32dev  # or nodemcu-32s, esp32-c3-devkitm-1, etc.

# WiFi Configuration
wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"
  
  # Fallback Access Point
  ap:
    ssid: "Garage-Motion-Fallback"
    password: "12345678"

# Enable logging
logger:

# Enable Home Assistant API (REQUIRED for Sentinel-ESP)
api:
  password: "your_api_password"
  # OR use encryption key (newer ESPHome):
  # encryption:
  #   key: "your_32_character_base64_key="

# Enable OTA updates
ota:
  password: "your_ota_password"

# Status LED (optional)
status_led:
  pin: GPIO2

# Motion Sensor
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO4
      mode: INPUT_PULLUP
    name: "Motion"
    device_class: motion
    filters:
      - delayed_off: 10s  # Stay triggered for 10 seconds
```

### Door/Window Contact Sensor

**File:** `front_door.yaml`

```yaml
esphome:
  name: front-door
  friendly_name: "Front Door Sensor"

esp8266:
  board: d1_mini

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

api:
  password: "your_api_password"

ota:
  password: "your_ota_password"

# Door Contact Sensor (normally closed)
binary_sensor:
  - platform: gpio
    pin:
      number: D5
      mode: INPUT_PULLUP
      inverted: true  # HIGH when closed, LOW when open
    name: "Door Contact"
    device_class: door
    filters:
      - delayed_on: 100ms  # Debounce
```

### Smart Switch/Light

**File:** `outdoor_light.yaml`

```yaml
esphome:
  name: outdoor-light
  friendly_name: "Outdoor Light"

esp32:
  board: esp32dev

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

api:
  password: "your_api_password"

ota:
  password: "your_ota_password"

# Relay Switch
switch:
  - platform: gpio
    pin: GPIO12
    name: "Outdoor Light"
    id: outdoor_light
    restore_mode: RESTORE_DEFAULT_OFF
```

### Dimmable Light

**File:** `dimmable_light.yaml`

```yaml
esphome:
  name: dimmable-light
  friendly_name: "Dimmable Light"

esp32:
  board: esp32dev

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

api:
  password: "your_api_password"

ota:
  password: "your_ota_password"

# PWM Dimmable Light
light:
  - platform: ledc
    pin: GPIO13
    name: "Dimmable Light"
    id: dimmable_light
```

### Flash the Device

```bash
# Install ESPHome (if not installed)
pip install esphome

# Compile and flash
esphome run garage_motion.yaml

# Or upload via OTA after first flash
esphome upload garage_motion.yaml --device garage-motion.local
```

---

## Matter Device Setup

### Prerequisites
- ESP32-C3, ESP32-C6, or ESP32-H2 board
- ESPHome 2023.11.0 or newer
- Matter-compatible controller (Sentinel-ESP)

### Matter Motion Sensor

**File:** `matter_motion.yaml`

```yaml
esphome:
  name: matter-motion
  friendly_name: "Matter Motion Sensor"
  platformio_options:
    board_build.partitions: partitions-4MB.csv

# Must use ESP32-C3/C6/H2 for Matter
esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf  # Required for Matter

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

# Enable Matter Protocol
esp32_matter:
  enable: true

# Status LED
status_led:
  pin: GPIO8

# Motion Sensor (automatically becomes Matter Occupancy Sensor)
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO4
      mode: INPUT_PULLUP
    name: "Motion"
    device_class: motion
    filters:
      - delayed_off: 10s

# Factory Reset Button
button:
  - platform: factory_reset
    name: "Factory Reset"
```

### Matter Door Contact

**File:** `matter_door.yaml`

```yaml
esphome:
  name: matter-door
  friendly_name: "Matter Door Sensor"

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

esp32_matter:
  enable: true

status_led:
  pin: GPIO8

# Door Contact (automatically becomes Matter Contact Sensor)
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO5
      mode: INPUT_PULLUP
      inverted: true
    name: "Door Contact"
    device_class: door

button:
  - platform: factory_reset
    name: "Factory Reset"
```

### Matter Smart Switch

**File:** `matter_switch.yaml`

```yaml
esphome:
  name: matter-switch
  friendly_name: "Matter Smart Switch"

esp32:
  board: esp32-c6-devkitc-1
  framework:
    type: esp-idf

wifi:
  ssid: "YourWiFiName"
  password: "YourWiFiPassword"

logger:

esp32_matter:
  enable: true

# Switch (automatically becomes Matter On/Off Device)
switch:
  - platform: gpio
    pin: GPIO12
    name: "Smart Switch"
    restore_mode: RESTORE_DEFAULT_OFF

button:
  - platform: factory_reset
    name: "Factory Reset"
```

### Flash Matter Device

```bash
esphome run matter_motion.yaml
```

After flashing, check logs for QR code:
```
[esp32_matter] QR Code: MT:Y.K9QAO00KNY0648G00
[esp32_matter] Manual Code: 34970112332
```

**Save this code!** You'll need it to pair with Sentinel-ESP.

---

## Connecting to Sentinel-ESP

### Method 1: ESPHome Native API

**Step 1: Find Device IP**
```bash
# Check your router's DHCP leases or use:
ping garage-motion.local
```

**Step 2: Add to Sentinel Web UI**

1. Access Sentinel web UI: `https://sentinel.local` or `https://192.168.1.100`
2. Navigate to **Settings → ESPHome Devices**
3. Click **Add Device**
4. Enter:
   - **Name:** Garage Motion
   - **IP Address:** 192.168.1.50
   - **Port:** 6053
   - **Password:** your_api_password
5. Click **Connect**

**Step 3: Map to Zone**

1. Click on connected device
2. Select **Map to Virtual Zone**
3. Choose zone number (33-96)
4. Click **Save**

Device is now active! Motion will trigger zone 33 when system is armed.

---

### Method 2: Matter Protocol

**Step 1: Commission Device**

1. Access Sentinel web UI: `https://sentinel.local`
2. Navigate to **Settings → Matter Devices**
3. Click **Commission New Device**
4. Enter the QR code or manual code from device logs:
   ```
   MT:Y.K9QAO00KNY0648G00
   ```
5. Wait 30-60 seconds for pairing
6. Device appears in list

**Step 2: Map to Zone/Relay**

For sensors:
1. Click device name
2. Select **Map to Virtual Zone**
3. Choose zone 33-96
4. Click **Save**

For switches/lights:
1. Click device name
2. Select **Map to Virtual Relay**
3. Choose relay 8-31
4. Click **Save**

---

### Method 3: MQTT (Alternative)

**Step 1: Configure ESPHome Device**

Add to your ESPHome YAML:
```yaml
mqtt:
  broker: 192.168.1.100  # Sentinel-ESP IP
  username: !secret mqtt_user
  password: !secret mqtt_pass
```

**Step 2: Configure Sentinel**

Currently auto-subscribed to Home Assistant MQTT topics. Custom MQTT topic configuration coming in future update.

---

## Zone and Relay Mapping

### Virtual Zones (33-96)

**Zone Types:**
- **33-48:** Motion sensors
- **49-64:** Door/window contacts
- **65-80:** Environmental (water, smoke, etc.)
- **81-96:** Reserved for future use

**Zone Configuration:**

Via Web UI:
1. Go to **Settings → Zones**
2. Find virtual zone (33-96)
3. Configure:
   - Name: "Garage Motion"
   - Type: Motion
   - Arm Mode: Away, Stay, Night
   - Entry Delay: 0 seconds (instant trigger)
   - Alert Priority: High

### Virtual Relays (8-31)

**Relay Types:**
- **8-15:** Smart lights
- **16-23:** Smart locks
- **24-31:** Other outputs (garage, gates, etc.)

**Relay Configuration:**

Via Web UI:
1. Go to **Settings → Relays**
2. Find virtual relay (8-31)
3. Configure:
   - Name: "Outdoor Flood Light"
   - Type: Light
   - Alarm Action: Turn ON
   - Disarm Action: Turn OFF

---

## Testing

### Test ESPHome Motion Sensor

**1. Verify Connection:**
```bash
# Check Sentinel logs
tail -f /var/log/sentinel.log | grep ESPHOME

# Should see:
# [ESPHOME_API] Connected to garage-motion (192.168.1.50)
# [ESPHOME_API] Subscribed to binary_sensor.motion
```

**2. Test Motion Detection:**
```bash
# Arm the system
curl -X POST https://sentinel.local/api/arm -d '{"mode":"away","pin":"1234"}'

# Wave hand in front of sensor
# Check logs:
# [ESPHOME_API] garage-motion: Motion DETECTED
# [ENGINE] Virtual Zone 33 triggered
# [ALARM] System entering ALARMED state
```

**3. Verify Web UI:**
- Go to **Dashboard → Zones**
- Zone 33 should show "TRIGGERED"
- Status should be "ALARMED"

### Test Matter Device

**1. Check Pairing Status:**
```bash
# Web UI: Settings → Matter Devices
# Should show:
# ✓ Garage Motion (0x1234567890ABCDEF) - ONLINE
```

**2. Test Sensor:**
```bash
# Arm system
curl -X POST https://sentinel.local/api/arm -d '{"mode":"away","pin":"1234"}'

# Trigger sensor (wave hand)
# Check web UI - zone 33 should trigger
```

**3. Test Smart Light:**
```bash
# Trigger alarm manually
curl -X POST https://sentinel.local/api/trigger

# Check that virtual relay 8 turns on
# Light should illuminate at 100% brightness
```

### Test Alarm Response

**Full Integration Test:**

1. **Setup:**
   - Map motion sensor to zone 33
   - Map smart light to relay 8
   - Arm system in AWAY mode

2. **Trigger:**
   - Walk in front of motion sensor
   
3. **Expected Behavior:**
   - Zone 33 triggers immediately (no entry delay)
   - Alarm siren activates (physical relay)
   - Smart light turns ON at 100% (virtual relay 8)
   - Email/Telegram/Noonlight alerts sent
   - Web UI shows ALARMED state

4. **Disarm:**
   - Enter PIN on keypad or web UI
   - Siren stops
   - Smart light turns OFF
   - System returns to DISARMED

---

## Troubleshooting

### ESPHome Device Won't Connect

**Check 1: Network Connectivity**
```bash
ping garage-motion.local
# Should get response
```

**Check 2: API Enabled**
```yaml
# Verify in ESPHome YAML:
api:
  password: "your_password"
```

**Check 3: Firewall**
```bash
# Port 6053 must be open
# On Sentinel, check:
netstat -an | grep 6053
```

**Check 4: Password Match**
- Password in ESPHome YAML must match password in Sentinel config
- Case-sensitive!

### Matter Device Won't Commission

**Check 1: Device in Pairing Mode**
- Look for QR code in logs
- Device LED should be blinking
- Try factory reset button

**Check 2: Correct Chip**
- Must be ESP32-C3, C6, or H2
- Original ESP32 does NOT support Matter

**Check 3: Network**
- Device and Sentinel must be on same WiFi/VLAN
- Matter uses mDNS for discovery

**Check 4: Timeout**
- Commissioning takes 30-60 seconds
- Don't refresh page during pairing

### Sensor Not Triggering Alarm

**Check 1: Zone Mapping**
```bash
# Web UI: Settings → ESPHome Devices
# Verify: "Garage Motion → Zone 33"
```

**Check 2: System Armed**
```bash
# Web UI: Dashboard
# Status must be: ARMED (AWAY/STAY/NIGHT)
```

**Check 3: Zone Configuration**
```bash
# Web UI: Settings → Zones → Zone 33
# Verify: Arm Mode includes current mode (AWAY)
```

**Check 4: Sensor State**
```bash
# Check ESPHome logs:
esphome logs garage_motion.yaml

# Should see when motion detected:
# [binary_sensor] 'Motion': Sending state ON
```

### Light Not Turning On During Alarm

**Check 1: Relay Mapping**
```bash
# Web UI: Settings → Matter Devices
# Verify: "Outdoor Light → Relay 8"
```

**Check 2: Relay Configuration**
```bash
# Web UI: Settings → Relays → Relay 8
# Verify: Alarm Action = "Turn ON"
```

**Check 3: Device Online**
```bash
# Web UI: Settings → Matter Devices
# Status should be: ONLINE (green)
```

**Check 4: Manual Control**
```bash
# Try manual control:
curl -X POST https://sentinel.local/api/relay/toggle -d '{"id":8}'

# Light should turn on/off
# If not, device connection issue
```

### Device Shows Offline

**ESPHome:**
```bash
# Check if device responds:
ping garage-motion.local

# Check Sentinel connection:
tail -f /var/log/sentinel.log | grep "garage-motion"

# Restart ESPHome device (power cycle)
```

**Matter:**
```bash
# Check Matter fabric:
# Web UI: Settings → Matter Devices → Diagnostics

# If offline >5 minutes, try:
# 1. Reboot Matter device
# 2. Reboot Sentinel-ESP
# 3. Re-commission if still offline
```

---

## Best Practices

### Security

1. **Use Strong Passwords:**
   ```yaml
   api:
     password: "use_random_32_char_password_here"
   ```

2. **Enable Encryption (ESPHome 2023.11+):**
   ```yaml
   api:
     encryption:
       key: "base64_encoded_32_byte_key="
   ```

3. **Separate Network:**
   - Put IoT devices on separate VLAN
   - Allow only Sentinel-ESP to communicate

### Reliability

1. **Static IP Addresses:**
   ```yaml
   wifi:
     manual_ip:
       static_ip: 192.168.1.50
       gateway: 192.168.1.1
       subnet: 255.255.255.0
   ```

2. **Strong WiFi Signal:**
   - Keep devices within good range
   - Use WiFi repeaters if needed
   - Monitor RSSI in device logs

3. **Backup Power:**
   - Use battery backup for critical sensors
   - ESPHome supports deep sleep for battery operation

### Maintenance

1. **Regular Updates:**
   ```bash
   # Update ESPHome devices monthly
   esphome run garage_motion.yaml
   ```

2. **Monitor Logs:**
   ```bash
   # Check for errors/warnings
   tail -f /var/log/sentinel.log | grep -E "ERROR|WARN"
   ```

3. **Test Monthly:**
   - Arm system and trigger each sensor
   - Verify alarm response
   - Check all lights/locks respond

---

## Advanced Configuration

### Multiple Sentinel Controllers

You can connect ESPHome devices to multiple controllers:

```yaml
api:
  # Primary - Sentinel-ESP
  services:
    - service: trigger_alarm
      then:
        - logger.log: "Alarm triggered from Sentinel"

# Also works with Home Assistant simultaneously
# Both can control the same device
```

### Custom Automations

In ESPHome, add custom logic:

```yaml
binary_sensor:
  - platform: gpio
    pin: GPIO4
    name: "Motion"
    on_press:
      then:
        - logger.log: "Motion detected"
        # Custom action: flash LED 3 times
        - repeat:
            count: 3
            then:
              - light.turn_on: status_led
              - delay: 100ms
              - light.turn_off: status_led
              - delay: 100ms
```

### Battery-Powered Sensors

For battery operation:

```yaml
deep_sleep:
  run_duration: 5s
  sleep_duration: 10min

# Wake on motion
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO4
      mode: INPUT_PULLUP
    name: "Motion"
    on_press:
      - deep_sleep.prevent
```

---

## Support

### Documentation
- ESPHome: https://esphome.io
- Matter: https://csa-iot.org/all-solutions/matter/
- Sentinel-ESP: See [README.md](README.md)

### Common Issues
- See [Troubleshooting](#troubleshooting) section above
- Check GitHub Issues
- Post in Home Assistant forums

### Contributing
- Report bugs on GitHub
- Submit pull requests
- Share your device configurations

---

## Quick Reference

### ESPHome Device Requirements
- ✅ WiFi connection
- ✅ `api:` section in YAML
- ✅ Static or reachable IP
- ✅ Port 6053 open

### Matter Device Requirements
- ✅ ESP32-C3, C6, or H2 chip
- ✅ `esp32_matter:` enabled
- ✅ ESPHome 2023.11.0+
- ✅ WiFi connection

### Virtual Resources
- **Zones:** 33-96 (64 available)
- **Relays:** 8-31 (24 available)
- **Physical remains:** 0-31 zones, 0-7 relays

### Web UI Access
```
HTTP:  http://sentinel.local
HTTPS: https://sentinel.local
IP:    https://192.168.1.100
```

### API Endpoints
```bash
# List ESPHome devices
GET /api/esphome/devices

# List Matter devices
GET /api/matter/devices

# Trigger test
POST /api/test/zone -d '{"zone_id":33}'
```

---

## What's Next?

1. **Set up your first sensor** - Start with motion sensor
2. **Test the integration** - Arm system and trigger
3. **Add more devices** - Expand coverage
4. **Configure automations** - Lights, locks, etc.
5. **Monitor and maintain** - Check logs, update firmware

Your Sentinel-ESP alarm system is now ready for wireless expansion! 🎉
