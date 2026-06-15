# Zigbee Phase 2 Hardware Testing Guide

## Overview
This guide covers testing of Zigbee Phase 2 advanced features including RGB color control, color temperature, smart locks, thermostats, battery monitoring, and network statistics.

## Prerequisites

### Hardware Required
- ESP32 board (KC868-A8 or compatible)
- CC2652P Zigbee coordinator (or compatible)
- USB cable for programming ESP32
- Serial cable connecting coordinator to ESP32 GPIO16/17

### Test Devices Required
At least one device from each category:
1. **RGB Color Light** - Full color control (e.g., Philips Hue bulb, IKEA Tradfri RGB)
2. **Tunable White Light** - Color temperature control (e.g., IKEA Tradfri white spectrum)
3. **Smart Lock** - Door lock control (e.g., Yale Assure, Schlage Encode)
4. **Thermostat** - Temperature control (e.g., Sinopé, Eurotronic Spirit)
5. **Battery Sensor** - Low battery alerts (e.g., door/window sensor, motion sensor)

### Software Tools
- Web browser for accessing control UI
- `curl` for API testing
- Serial terminal (screen, minicom) for logs

## Hardware Setup

### 1. Coordinator Connection
```
ESP32 GPIO16 (RX) ← TX ─┐
ESP32 GPIO17 (TX) → RX ─┤ CC2652P Coordinator
ESP32 GND ←→ GND ───────┘
```

Verify UART communication:
```bash
# Check ESP32 logs for coordinator initialization
minicom -D /dev/ttyUSB0 -b 115200
# Look for: "ZIGBEE: Coordinator initialized successfully"
```

### 2. Power Up Sequence
1. Connect coordinator to ESP32
2. Power on ESP32
3. Wait 5 seconds for coordinator initialization
4. Check web UI at http://esp32-ip/zigbee.html

## Device Pairing

### Pairing Procedure
1. Open http://esp32-ip/zigbee.html
2. Click "Pair Device" button (60-second window)
3. Put Zigbee device into pairing mode:
   - **Lights**: Power cycle 5 times (on 2s, off 2s)
   - **Sensors**: Press reset button 5 times rapidly
   - **Locks**: Follow manufacturer pairing instructions
   - **Thermostats**: Menu → Settings → Network → Join Network

4. Device should appear in web UI within 30 seconds
5. Verify device type is correctly identified

## Phase 2 Feature Testing

### Test 1: RGB Color Control

**Supported Devices**: Color lights with Color Control cluster (0x0300)

#### Web UI Test
1. Navigate to RGB light device card
2. Verify color picker is displayed
3. Select red color (#FF0000)
4. Device should change to red immediately
5. Try additional colors:
   - Green (#00FF00)
   - Blue (#0000FF)
   - Yellow (#FFFF00)
   - Cyan (#00FFFF)
   - Magenta (#FF00FF)
   - White (#FFFFFF)

#### API Test
```bash
# Set color to purple (0xFF00FF)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"color","value":16711935}'

# Expected log output:
# ZIGBEE: Setting device 0x1234 color: RGB(0xFF00FF) -> H:149 S:254
```

#### Verification
- [ ] Color picker appears for RGB lights
- [ ] Color changes are immediate (< 1 second)
- [ ] RGB → HSV conversion is correct
- [ ] Device maintains color after power cycle
- [ ] API returns success (200 OK)

**Expected Behavior**: Device should display the selected color within 1 second. Hue should wrap correctly (0-254 range).

**Troubleshooting**:
- Color not changing: Check if device supports Color Control cluster
- Wrong color: Verify RGB hex input is correct
- Delayed response: Check Zigbee network congestion

---

### Test 2: Color Temperature Control

**Supported Devices**: Tunable white lights with Color Control cluster (0x0300)

#### Web UI Test
1. Navigate to tunable white light device card
2. Verify color temp slider is displayed (2000K-6500K)
3. Set to 6500K (cool white/daylight)
4. Device should show cool white light
5. Set to 2700K (warm white/incandescent)
6. Device should show warm white light
7. Try intermediate values: 4000K, 5000K

#### API Test
```bash
# Set to 4000K (250 mireds = 1000000/4000)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"color_temp","value":250}'

# Expected log output:
# ZIGBEE: Setting device 0x1234 color temp to 250 mireds (~4000K)
```

#### Verification
- [ ] Color temp slider appears for tunable lights
- [ ] Slider range is 2000K-6500K
- [ ] Light color changes smoothly
- [ ] Kelvin ↔ mireds conversion is correct
- [ ] API validates mireds range (153-500)

**Expected Behavior**: Light should transition smoothly between warm and cool white. Kelvin to mireds formula: `mireds = 1000000 / kelvin`.

**Troubleshooting**:
- Temp not changing: Verify device supports color temp attribute
- Out of range error: Some devices have limited range (e.g., 250-454 mireds)
- Wrong color: Check if device is in color mode vs. temp mode

---

### Test 3: Smart Lock Control

**Supported Devices**: Smart locks with Door Lock cluster (0x0101)

#### Web UI Test
1. Navigate to smart lock device card
2. Verify lock/unlock button is displayed
3. Current state should show LOCKED or UNLOCKED
4. Click "Lock" button
5. Verify lock actuates (listen for motor sound)
6. State badge should update to "LOCKED"
7. Click "Unlock" button
8. Verify lock retracts
9. State badge should update to "UNLOCKED"

#### API Test
```bash
# Lock the door
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"lock","value":"locked"}'

# Unlock the door
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"lock","value":"unlocked"}'

# Expected log output:
# ZIGBEE: Setting device 0x1234 lock state: LOCKED/UNLOCKED
```

#### Verification
- [ ] Lock button appears for lock devices
- [ ] Button shows current state (LOCKED/UNLOCKED)
- [ ] Lock actuates when commanded
- [ ] State updates within 2 seconds
- [ ] API returns success for valid commands
- [ ] Invalid commands return 400 error

**Expected Behavior**: Lock should actuate within 2 seconds. Listen for motor engagement. Some locks may beep or flash LED.

**Troubleshooting**:
- Lock not responding: Check battery level (may refuse if <20%)
- Delayed response: Lock may take 3-5 seconds for security
- API error: Verify lock supports Door Lock cluster
- Jam detected: Check physical obstruction

---

### Test 4: Thermostat Control

**Supported Devices**: Thermostats with Thermostat cluster (0x0201)

#### Web UI Test
1. Navigate to thermostat device card
2. Verify temperature input is displayed
3. Current setpoint should be shown (e.g., 21.0°C)
4. Enter new temperature (e.g., 22.5°C)
5. Press Enter or click outside input
6. Thermostat should update setpoint
7. Display should show new temperature within 5 seconds
8. Try boundary values:
   - Minimum: 10.0°C
   - Maximum: 35.0°C
   - Invalid: 40.0°C (should be rejected)

#### API Test
```bash
# Set thermostat to 21.5°C
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"thermostat","value":21.5}'

# Expected log output:
# ZIGBEE: Setting thermostat 0x1234 to 21.5°C

# Test out-of-range (should fail)
curl -X POST http://esp32-ip/api/zigbee/device/0x1234/control \
  -H "Content-Type: application/json" \
  -d '{"action":"thermostat","value":40.0}'
# Expected: 400 Bad Request
```

#### Verification
- [ ] Temperature input appears for thermostats
- [ ] Range validation works (10-35°C)
- [ ] Thermostat display updates correctly
- [ ] Setpoint persists after ESP32 reboot
- [ ] API validates temperature range
- [ ] Float values work (e.g., 21.5°C)

**Expected Behavior**: Thermostat should update setpoint display within 5 seconds. Some thermostats may take 10-15 seconds to adjust heating/cooling.

**Troubleshooting**:
- Setpoint not updating: Check thermostat mode (heat/cool/auto)
- Out of range error: Some thermostats have narrower range
- Temperature format: Verify 0.01°C units (2100 = 21.00°C)
- No response: Check if thermostat is in manual mode

---

### Test 5: Battery Monitoring

**Supported Devices**: Battery-powered sensors with Power Configuration cluster (0x0001)

#### Web UI Test
1. Navigate to battery-powered sensor
2. Verify battery percentage is displayed (🔋 icon)
3. Battery level should be accurate (0-100%)
4. Low battery warning should appear if <20%
   - Icon changes to ⚠️
   - Background turns red
   - Flashing animation

#### API Test
```bash
# Get battery level for specific device
curl http://esp32-ip/api/zigbee/device/0x1234/battery

# Expected response:
# {"battery_level":85,"low_battery":false}

# Check network statistics for low battery devices
curl http://esp32-ip/api/zigbee/statistics

# Expected response includes:
# {"low_battery_devices":2,...}
```

#### Verification
- [ ] Battery percentage displays correctly
- [ ] Low battery alert appears (<20%)
- [ ] Alert is visible (red background, flashing)
- [ ] Battery endpoint returns correct level
- [ ] Statistics count low battery devices
- [ ] Logs show low battery warnings

**Expected Behavior**: Battery level should update every 30-60 minutes. Low battery alerts should appear in ESP32 logs every 5 minutes.

**Troubleshooting**:
- Battery not showing: Device may not report battery percentage
- Wrong percentage: Some devices report in voltage, not percentage
- No low battery alert: Check threshold (default 20%)
- API returns -1: Device doesn't have ZIGBEE_CAP_BATTERY

---

### Test 6: Network Statistics

**Supported Features**: Real-time network health monitoring

#### Web UI Test
1. Open http://esp32-ip/zigbee.html
2. Statistics panel should appear above action bar
3. Verify all metrics are displayed:
   - **Online**: Count of connected devices
   - **Offline**: Count of disconnected devices
   - **Commands**: Total commands sent
   - **Success Rate**: (commands_sent - commands_failed) / commands_sent
   - **Low Battery**: Count of devices <20% battery
   - **Uptime**: Network uptime (hours:minutes)

4. Control a device (turn on/off)
5. Refresh page
6. Commands count should increase
7. Success rate should remain high (>95%)

#### API Test
```bash
# Get network statistics
curl http://esp32-ip/api/zigbee/statistics

# Expected response:
{
  "total_devices": 8,
  "online_devices": 7,
  "offline_devices": 1,
  "commands_sent": 523,
  "commands_failed": 2,
  "frames_received": 1847,
  "low_battery_devices": 2,
  "uptime_seconds": 14523
}

# Calculate success rate:
# success_rate = ((523 - 2) / 523) * 100 = 99.6%
```

#### Verification
- [ ] Statistics panel displays on page load
- [ ] All 6 metrics are shown
- [ ] Online/offline counts are accurate
- [ ] Commands increment after device control
- [ ] Success rate is calculated correctly
- [ ] Low battery count matches devices <20%
- [ ] Uptime increases monotonically
- [ ] Statistics refresh every 10 seconds

**Expected Behavior**: Statistics should update automatically every 10 seconds. Success rate should be >95% for healthy network.

**Troubleshooting**:
- Statistics not showing: Check if any devices are paired
- Commands not incrementing: Verify command tracking is working
- Low success rate: Check Zigbee signal strength, interference
- Uptime resets: ESP32 rebooted (check logs for crash)

---

## Integration Testing

### Test Scenario 1: Multi-Device Control
1. Pair 5+ devices of different types
2. Control all devices simultaneously using web UI
3. Verify all devices respond correctly
4. Check statistics for command count and success rate

### Test Scenario 2: Battery Alert Flow
1. Use device simulator or low-battery sensor
2. Wait for battery level to drop below 20%
3. Verify low battery alert appears in web UI
4. Check ESP32 logs for battery warnings
5. Verify statistics show correct low battery count

### Test Scenario 3: Network Recovery
1. Disconnect coordinator power
2. Verify devices go offline in web UI
3. Reconnect coordinator
4. Devices should come back online within 30 seconds
5. Statistics should show brief spike in offline count

### Test Scenario 4: Command Tracking
1. Record initial command count from statistics
2. Execute 10 device commands (on/off, brightness, etc.)
3. Refresh statistics
4. Commands sent should increase by 10
5. Success rate should remain >95%

---

## Performance Benchmarks

### Expected Performance Targets
- **Command Response Time**: <1 second (light on/off, brightness)
- **Color Change**: <1 second (RGB lights)
- **Lock Actuation**: <3 seconds (smart locks)
- **Thermostat Update**: <5 seconds (setpoint display)
- **Battery Update**: Every 30-60 minutes
- **Statistics Refresh**: Every 10 seconds
- **Success Rate**: >95% (healthy network)

### Network Load Testing
Test with increasing device counts:
- 5 devices: Success rate >99%
- 10 devices: Success rate >98%
- 20 devices: Success rate >95%
- 30+ devices: May need coordinator upgrade

---

## Common Issues & Solutions

### RGB Color Issues
**Problem**: Color not matching web UI picker
**Solution**: 
- Verify RGB → HSV conversion (check logs for H:0-254, S:0-254)
- Some devices may not support full color gamut
- Check if device is in color mode (not color temp mode)

### Color Temperature Issues
**Problem**: Light doesn't respond to temp changes
**Solution**:
- Switch device to color temp mode (not RGB mode)
- Check device supported range (may be narrower than 153-500 mireds)
- Some devices need separate "mode" command

### Smart Lock Issues
**Problem**: Lock not responding to commands
**Solution**:
- Check battery level (locks may refuse commands if low)
- Verify physical obstruction (door alignment)
- Some locks require PIN code for remote unlock
- Check if lock is in "manual override" mode

### Thermostat Issues
**Problem**: Setpoint changes but heating doesn't activate
**Solution**:
- Check thermostat mode (heat/cool/off)
- Verify thermostat is not in "away" mode
- Some thermostats have hysteresis (dead band)
- Check local temperature vs. setpoint difference

### Battery Monitoring Issues
**Problem**: Battery level shows -1 or not updating
**Solution**:
- Verify device reports battery percentage (not all do)
- Battery updates may be infrequent (30-60 minutes)
- Check if device has ZIGBEE_CAP_BATTERY capability
- Some devices report voltage instead of percentage

### Statistics Issues
**Problem**: Success rate is low (<90%)
**Solution**:
- Check Zigbee coordinator signal strength
- Look for RF interference (Wi-Fi on same 2.4GHz channel)
- Verify coordinator firmware is up to date
- Reduce distance between devices and coordinator
- Add Zigbee routers (powered devices) to improve mesh

---

## Logging & Debugging

### Enable Verbose Zigbee Logs
```c
// In components/zigbee/zigbee_mgr.c
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
```

### Check Command Tracking
```bash
# Filter logs for command statistics
minicom -D /dev/ttyUSB0 -b 115200 | grep "commands_sent\|commands_failed"
```

### Monitor Battery Warnings
```bash
# Filter logs for low battery alerts
minicom -D /dev/ttyUSB0 -b 115200 | grep "Low battery"
```

### Check Zigbee Frame Reception
```bash
# Monitor incoming Zigbee frames
minicom -D /dev/ttyUSB0 -b 115200 | grep "frames_received"
```

---

## Success Criteria

### Phase 2 Complete When:
- [ ] All 5 device types tested successfully
- [ ] RGB color control works correctly
- [ ] Color temperature control works correctly
- [ ] Smart lock control works correctly
- [ ] Thermostat control works correctly
- [ ] Battery monitoring alerts work
- [ ] Statistics panel displays accurate data
- [ ] Command success rate >95%
- [ ] All API endpoints return correct responses
- [ ] Web UI displays all new controls
- [ ] No memory leaks after 24-hour run
- [ ] Documentation is complete and accurate

---

## Next Steps: Phase 3 (if applicable)
- Scene control (save/recall device states)
- Group control (control multiple devices at once)
- Automation rules (if X then Y)
- Device OTA firmware updates
- Zigbee binding (device-to-device control)
- Energy monitoring (for power measurement devices)

---

## Support & Resources

### Zigbee Cluster IDs
- 0x0300: Color Control
- 0x0101: Door Lock
- 0x0201: Thermostat
- 0x0001: Power Configuration (battery)

### Zigbee Command Reference
- Color: Move to Hue and Saturation (0x06)
- Color Temp: Move to Color Temperature (0x0A)
- Lock: Lock Door (0x00) / Unlock Door (0x01)
- Thermostat: Write Attributes (0x02)

### Useful Links
- Zigbee Alliance Specifications: https://zigbeealliance.org
- CC2652P Coordinator: https://www.ti.com/product/CC2652P
- ESP32 GPIO Reference: https://docs.espressif.com/projects/esp-idf/

---

**Document Version**: 1.0  
**Last Updated**: 2024-01-30  
**Author**: Sentinel ESP Zigbee Team  
**Status**: Phase 2 Complete - Ready for Testing
