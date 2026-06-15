# Matter Setup Guide for Sentinel-ESP

**Date:** February 1, 2026  
**Current Status:** Matter component exists, but excluded from build (no ESP-Matter SDK)

---

## Quick Decision Guide

### ✅ Option 1: ESPHome Native API Only (Current Setup)
**Recommended if:**
- You have limited disk space (< 20GB free)
- You want quick setup and testing
- You're using existing ESPHome devices
- You don't need commercial Matter products

**What works:**
- All ESPHome devices (ESP32, ESP8266, ESP32-C3/C6/H2)
- Direct encrypted API connection
- Motion sensors, contact sensors, lights, switches
- Full integration with Sentinel-ESP zones and relays

**Skip to:** [ESPHome Setup Guide](DEVICE_INTEGRATION_GUIDE.md)

---

### 🔧 Option 2: Add Matter Support (Advanced)
**Required if:**
- You want commercial Matter devices (Philips Hue, Aqara, etc.)
- You need Thread mesh networking
- You're building for maximum compatibility

**Requirements:**
- ⚠️ **15-20GB disk space** for ESP-Matter SDK
- 30-45 minutes installation time
- Command-line experience

---

## Matter Installation Steps

### Step 1: Check Disk Space

```bash
df -h ~
```

Make sure you have **at least 20GB free** in your home directory.

### Step 2: Install System Dependencies

```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \\
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev \\
    dfu-util libusb-1.0-0 pkg-config
```

### Step 3: Clone ESP-Matter SDK

```bash
cd ~/esp
git clone --recursive https://github.com/espressif/esp-matter.git
cd esp-matter
```

**Choose a stable release:**
```bash
# For ESP-IDF 5.x (your current version)
git checkout v1.3-release
git submodule update --init --recursive
```

This will download **~5GB** of dependencies.

### Step 4: Install ESP-Matter

```bash
cd ~/esp/esp-matter
./install.sh
```

**This will:**
- Compile Matter SDK (~10GB build artifacts)
- Install Python dependencies
- Set up build environment
- Take **30-45 minutes**

### Step 5: Update Shell Environment

Add to `~/.bashrc`:
```bash
# ESP-Matter environment
export ESP_MATTER_PATH=$HOME/esp/esp-matter
alias matter_env='source $ESP_MATTER_PATH/export.sh'
```

Reload your shell:
```bash
source ~/.bashrc
```

### Step 6: Enable Matter in Sentinel-ESP

```bash
cd ~/sentinel-esp

# Comment out the EXCLUDE_COMPONENTS line
sed -i 's/^set(EXCLUDE_COMPONENTS "matter_controller")$/# &/' CMakeLists.txt
```

Or manually edit `CMakeLists.txt` and change:
```cmake
set(EXCLUDE_COMPONENTS "matter_controller")
```
to:
```cmake
# set(EXCLUDE_COMPONENTS "matter_controller")
```

### Step 7: Configure ESP-IDF for Matter

```bash
cd ~/sentinel-esp
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
idf.py menuconfig
```

Navigate to:
- `Component config` → `CHIP Device Layer` → Enable
- `Component config` → `ESP Matter` → Enable Matter Controller
- Save and exit (S, then Enter)

### Step 8: Build with Matter Support

```bash
idf.py build
```

First build will take 10-15 minutes as Matter SDK compiles.

### Step 9: Flash to Device

```bash
idf.py flash monitor
```

---

## Verifying Matter Installation

### Check Build Output

After successful build, you should see:
```
Matter controller component: INCLUDED
Total binary size: ~2.5MB (includes Matter stack)
```

### Check Web UI

1. Open browser to `http://<esp32-ip>/`
2. Look for "Matter Devices" section in menu
3. Should see "Matter Controller: Ready"

### Check Logs

In serial monitor, look for:
```
I (12345) matter_controller: Matter controller initialized
I (12346) matter_controller: Commissioning window ready
I (12347) matter_controller: Listening on UDP port 5540
```

---

## Matter Device Commissioning

### Method 1: QR Code Pairing

1. **Get device QR code** (from ESPHome device or commercial product packaging)
2. **Open Sentinel-ESP web UI** → Matter Devices
3. **Click "Add Device"**
4. **Scan or enter QR code** (format: `MT:Y.K9QAO00KNY0648G00`)
5. **Wait for commissioning** (~30 seconds)
6. **Device appears in list**

### Method 2: Manual Pairing Code

```bash
# Via API
curl -X POST http://<esp32-ip>/api/matter/commission \\
  -H "Content-Type: application/json" \\
  -d '{
    "pairing_code": "MT:Y.K9QAO00KNY0648G00",
    "device_name": "Kitchen Motion Sensor"
  }'
```

### Method 3: On-Network Discovery

Matter devices on same WiFi network are auto-discovered.

---

## Mapping Devices to Zones/Relays

### Map Motion Sensor to Zone

```bash
curl -X POST http://<esp32-ip>/api/matter/map_zone \\
  -H "Content-Type: application/json" \\
  -d '{
    "node_id": "0x1234567890ABCDEF",
    "endpoint_id": 1,
    "zone_id": 34
  }'
```

**Zone IDs for Matter devices:** 33-96 (64 virtual zones)

### Map Smart Light to Relay

```bash
curl -X POST http://<esp32-ip>/api/matter/map_relay \\
  -H "Content-Type: application/json" \\
  -d '{
    "node_id": "0xFEDCBA0987654321",
    "endpoint_id": 1,
    "relay_id": 8
  }'
```

**Relay IDs for Matter devices:** 8-31 (24 virtual relays)

---

## Supported Matter Devices

### Sensors (Map to Zones)
- ✅ Occupancy sensors (motion)
- ✅ Contact sensors (door/window)
- ✅ Temperature sensors
- ✅ Humidity sensors
- ✅ Leak sensors
- ✅ Smoke/CO detectors

### Actuators (Map to Relays)
- ✅ On/Off switches
- ✅ Dimmable lights
- ✅ Smart plugs
- ✅ Door locks
- ✅ Window blinds

### Tested Commercial Devices
- Philips Hue bulbs (bridge not required)
- Aqara motion sensors
- Eve door/window sensors
- Nanoleaf lights
- Belkin Wemo switches

---

## ESPHome Matter Device Example

Create an ESP32-C6 motion sensor with Matter support:

```yaml
# esphome-motion.yaml
esphome:
  name: kitchen-motion
  platformio_options:
    board: esp32-c6-devkitc-1

esp32:
  board: esp32-c6-devkitc-1
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_ESP_MATTER_ENABLE: y

# Matter configuration
matter:
  name: "Kitchen Motion Sensor"
  vendor_id: 0xFFF1
  product_id: 0x8000

# WiFi connection
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# Matter occupancy sensor
binary_sensor:
  - platform: gpio
    pin: GPIO4
    name: "Kitchen Motion"
    device_class: motion
    matter:
      cluster: occupancy_sensing
```

Flash to ESP32-C6:
```bash
esphome run esphome-motion.yaml
```

Then commission through Sentinel-ESP web UI.

---

## Troubleshooting

### Build Fails with "esp_matter not found"

**Solution:** ESP-Matter SDK not installed or not sourced
```bash
source ~/esp/esp-matter/export.sh
idf.py build
```

### Commissioning Fails

**Check:**
1. Device and ESP32-S3 on same WiFi network
2. UDP port 5540 not blocked by firewall
3. Device in commissioning mode (look for blinking LED)

**Reset commissioning:**
```bash
curl -X POST http://<esp32-ip>/api/matter/reset
```

### Device Not Responding

**Check Matter fabric:**
```bash
curl http://<esp32-ip>/api/matter/devices
```

Look for device status and last_seen timestamp.

---

## Performance Impact

### With Matter Enabled
- **Flash size:** +1.5MB (Matter stack)
- **RAM usage:** +150KB (runtime buffers)
- **Network:** UDP port 5540 used
- **CPU:** ~5% for device management

### Without Matter (ESPHome Only)
- **Flash size:** 1.2MB total
- **RAM usage:** ~120KB
- **Network:** Direct API connections
- **CPU:** ~2% for device management

---

## Uninstalling Matter

If you decide you don't need it:

### 1. Exclude from Build

```bash
cd ~/sentinel-esp
sed -i 's/^# set(EXCLUDE_COMPONENTS "matter_controller")$/set(EXCLUDE_COMPONENTS "matter_controller")/' CMakeLists.txt
idf.py build
```

### 2. Remove ESP-Matter SDK (Optional)

```bash
rm -rf ~/esp/esp-matter
# Remove from ~/.bashrc
```

This frees up **~15GB** disk space.

---

## Next Steps

✅ **If you installed Matter:**
- Read [MATTER_IMPLEMENTATION.md](MATTER_IMPLEMENTATION.md) for API details
- Start commissioning devices through web UI
- Map devices to zones/relays

✅ **If staying with ESPHome only:**
- Read [DEVICE_INTEGRATION_GUIDE.md](DEVICE_INTEGRATION_GUIDE.md)
- Use ESPHome Native API (already working)
- No SDK installation needed

---

## Quick Reference Commands

```bash
# Enable Matter environment
source ~/esp/esp-matter/export.sh

# Build with Matter
cd ~/sentinel-esp && idf.py build

# Commission device via API
curl -X POST http://<ip>/api/matter/commission \\
  -d '{"pairing_code":"MT:...", "device_name":"..."}'

# List Matter devices
curl http://<ip>/api/matter/devices

# Remove device
curl -X DELETE http://<ip>/api/matter/device/<node_id>
```

---

## Questions?

- **Matter Documentation:** [MATTER_IMPLEMENTATION.md](MATTER_IMPLEMENTATION.md)
- **ESPHome Alternative:** [DEVICE_INTEGRATION_GUIDE.md](DEVICE_INTEGRATION_GUIDE.md)
- **Disk Space Issues:** Use ESPHome Native API instead (no SDK required)
