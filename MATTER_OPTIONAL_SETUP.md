# Matter Support - Optional Installation

Matter protocol is **optional** in Sentinel-ESP. The system works fully with just ESPHome Native API for wireless devices.

---

## ⚠️ Important: Disk Space Requirement

**ESP-Matter SDK requires 15-20GB of disk space** for installation (submodules, build tools, dependencies).

If you have limited disk space, **use ESPHome Native API instead** - it requires no SDK installation and works with all ESP devices.

---

## Current Build Status

✅ **ESPHome Native API** - ACTIVE (works with all ESP32/ESP8266 devices)  
⚠️ **Matter Protocol** - DISABLED (requires ESP-Matter SDK + 15-20GB disk space)

**Default Configuration:** Matter is excluded from the build to simplify installation.

---

## When Do You Need Matter?

### Use ESPHome API Only (Recommended)
- You have existing ESPHome devices (any ESP chip)
- You want simplicity and fast setup
- You don't need commercial Matter devices

### Add Matter Support (Optional)
- You want to use ESP32-C3/C6/H2 devices with Matter
- You need commercial Matter products (smart bulbs, locks, etc.)
- You want Thread mesh networking
- You're building for future-proofing

---

## Installing ESP-Matter SDK (Optional)

If you decide you want Matter support:

### Step 1: Install Dependencies

```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
    dfu-util libusb-1.0-0
```

### Step 2: Clone ESP-Matter

```bash
cd ~/esp
git clone --recursive https://github.com/espressif/esp-matter.git
cd esp-matter
git checkout v1.3-release  # Use stable release
git submodule update --init --recursive
```

### Step 3: Install ESP-Matter

```bash
cd ~/esp/esp-matter
./install.sh
```

This will take 15-30 minutes to compile and install.

### Step 4: Export Environment

Add to `~/.bashrc`:
```bash
export ESP_MATTER_PATH=$HOME/esp/esp-matter
source $ESP_MATTER_PATH/export.sh
```

Then reload:
```bash
source ~/.bashrc
```

### Step 5: Enable Matter in Sentinel-ESP

```bash
cd ~/sentinel-esp

# Remove the exclusion from build
# Edit CMakeLists.txt and remove or comment out:
# set(EXCLUDE_COMPONENTS "matter_controller")
```

Or automatically with sed:
```bash
sed -i '/EXCLUDE_COMPONENTS.*matter_controller/s/^/# /' CMakeLists.txt
```

### Step 6: Configure and Build

```bash
idf.py menuconfig
# Navigate to: Component config → Sentinel-ESP
# Enable: [*] Enable Matter Controller Support
# Save and exit

idf.py build
idf.py flash monitor
```

---

## Verifying Matter Installation

### Check ESP-Matter Available

```bash
# This should NOT give an error:
ls $ESP_MATTER_PATH/components/esp_matter
```

### Check Build Includes Matter

```bash
cd ~/sentinel-esp
idf.py build 2>&1 | grep -i matter

# Should see:
# -- Adding component: matter_controller
# -- Component matter_controller registered
```

### Check Logs After Flashing

```bash
idf.py monitor

# Look for:
# I (2456) MAIN: Initializing Matter controller...
# I (2460) MATTER: Matter controller initialized
# I (2465) MAIN: Matter controller initialized
```

---

## Without Matter (Current Default)

System works perfectly without Matter:

### What You Have
✅ ESPHome Native API support (all ESP devices)  
✅ Virtual zones 33-96 for sensors  
✅ Virtual relays 8-31 for outputs  
✅ MQTT integration  
✅ Web UI device management  
✅ Linux test environment  

### What You're Missing
❌ Matter device commissioning (only needed for Matter devices)  
❌ Thread mesh networking (only needed for Thread devices)  
❌ Commercial Matter products (bulbs, locks from major brands)

### Build and Run Without Matter

```bash
cd ~/sentinel-esp
idf.py build
idf.py flash monitor

# Logs will show:
# I (2456) MAIN: Matter controller disabled (ESP-Matter SDK not installed)
# I (2460) MAIN: To enable: Install ESP-Matter SDK and set CONFIG_ENABLE_MATTER_CONTROLLER=y
```

Everything else works normally!

---

## Testing Without Matter

Linux test environment works without Matter SDK:

```bash
cd ~/sentinel-esp/test_linux
make clean && make
./sentinel_test

# Both ESPHome and Matter mocks load
# Web server starts on port 8000
# Test with: curl http://localhost:8000/api/esphome/devices
```

---

## Comparison: ESPHome API vs Matter

| Feature | ESPHome API | Matter |
|---------|-------------|---------|
| **ESP32 Support** | All (ESP32, ESP8266, C3, S2, S3) | C3, C6, H2 only |
| **Setup Difficulty** | Easy | Medium |
| **SDK Installation** | None (built-in) | Requires ESP-Matter |
| **Build Time** | Fast (~2 min) | Slower (~5 min) |
| **Device Pairing** | IP + Password | QR Code commissioning |
| **Commercial Devices** | ESPHome only | Any Matter device |
| **Recommended For** | Existing ESPHome | New installations |

---

## Recommendation

**For most users:** Use ESPHome Native API only (current default). It's simpler, faster, and works with all your existing devices.

**Add Matter later if:**
- You get ESP32-C3/C6/H2 devices
- You want commercial Matter products
- You have time for the installation (~30 min)

---

## Troubleshooting Matter Build

### Error: "Failed to resolve component 'esp_matter'"

✅ **This is expected!** Matter is disabled by default.

**Solution:** Follow installation steps above, or continue using ESPHome API.

### Error: "chip library not found"

ESP-Matter SDK not properly installed.

```bash
cd ~/esp/esp-matter
./install.sh
source ./export.sh
```

### Error: "CONFIG_ENABLE_MATTER_CONTROLLER not found"

You need to enable it in menuconfig:
```bash
idf.py menuconfig
# Component config → Sentinel-ESP → Enable Matter
```

---

## Quick Decision Tree

```
Do you have ESP32-C3/C6/H2 devices?
 └─ No → Use ESPHome API (current setup) ✅
 └─ Yes → Do you want to invest 30 min in Matter setup?
     └─ No → Use ESPHome API (works on C3/C6/H2 too) ✅
     └─ Yes → Follow installation guide above
```

---

## Summary

**Current State:**
- ✅ System builds and runs WITHOUT Matter
- ✅ ESPHome Native API fully functional
- ✅ All alarm features working
- ✅ Virtual zones/relays available

**If You Want Matter:**
- Install ESP-Matter SDK (~30 min)
- Enable in CMakeLists.txt
- Enable in menuconfig
- Rebuild

**Most users don't need Matter right now!** ESPHome API handles all your wireless devices.

---

## Support

- ESPHome API works out of the box - see [DEVICE_INTEGRATION_GUIDE.md](DEVICE_INTEGRATION_GUIDE.md)
- Matter installation issues - check ESP-Matter docs: https://github.com/espressif/esp-matter
- Questions - open GitHub issue

Your Sentinel-ESP is ready to use with ESPHome devices immediately! 🎉
