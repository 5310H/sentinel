# Wyze Floodlight v1 - Thingino Custom Scripts

Custom control scripts for Wyze Floodlight v1 running Thingino firmware. Provides GPIO-based control for LED floodlight, PIR motion sensor integration, and Sentinel alarm system connectivity.

## 📦 Package Contents

### 1. `gpio_discover.sh` - GPIO Pin Discovery
Automated script to identify GPIO pins for floodlight LEDs and PIR sensor.

**Features:**
- Scans available GPIO pins (0-95 for Ingenic T31)
- Tests PIR sensor by monitoring for motion activity
- Tests output GPIOs to find LED control pin
- Generates configuration file automatically
- Checks for existing control utilities

**Usage:**
```bash
# Copy to Thingino device
scp gpio_discover.sh root@192.168.1.101:/tmp/

# SSH into device and run
ssh root@192.168.1.101
cd /tmp
chmod +x gpio_discover.sh
./gpio_discover.sh

# Follow prompts:
# 1. Wave hand in front of PIR sensor when prompted
# 2. Watch floodlight LEDs during GPIO tests
# 3. Note discovered GPIO pins
```

### 2. `floodlight_control.sh` - Main Control Script
Complete floodlight control system with manual and automatic modes.

**Features:**
- ✅ Manual ON/OFF control
- ✅ Auto mode (motion-activated lighting)
- ✅ Lux threshold support (only activate when dark)
- ✅ Configurable timeout (default 5 minutes)
- ✅ MQTT notifications for Sentinel integration
- ✅ Configuration management
- ✅ Status monitoring
- ✅ Logging

**Installation:**
```bash
# Copy to device
scp floodlight_control.sh root@192.168.1.101:/tmp/

# Install to system
ssh root@192.168.1.101
mv /tmp/floodlight_control.sh /usr/sbin/floodlight_ctl
chmod +x /usr/sbin/floodlight_ctl

# Configure GPIO pins (after running gpio_discover.sh)
floodlight_ctl config set FLOODLIGHT_GPIO 52
floodlight_ctl config set PIR_SENSOR_GPIO 49
floodlight_ctl config set MQTT_BROKER 192.168.1.50
```

**Usage Examples:**
```bash
# Turn on floodlight manually
floodlight_ctl on

# Turn off
floodlight_ctl off

# Enable auto mode (motion-activated)
floodlight_ctl auto

# Check status
floodlight_ctl status

# Test floodlight (3-second blink)
floodlight_ctl test

# View/edit configuration
floodlight_ctl config show
floodlight_ctl config set AUTO_MODE_TIMEOUT 600  # 10 minutes
floodlight_ctl config set LUX_THRESHOLD 150
```

### 3. `sentinel_integration.sh` - Sentinel MQTT Bridge
Connects Thingino floodlight to Sentinel alarm system via MQTT.

**Features:**
- ✅ Publishes motion events to Sentinel
- ✅ Subscribes to floodlight commands from Sentinel
- ✅ Alarm-triggered activation
- ✅ Automatic snapshot on alarm
- ✅ Bidirectional state synchronization
- ✅ Cron or daemon mode

**Installation:**
```bash
# Copy to device
scp sentinel_integration.sh root@192.168.1.101:/tmp/

# Install to system
ssh root@192.168.1.101
mv /tmp/sentinel_integration.sh /usr/bin/sentinel_integration.sh
chmod +x /usr/bin/sentinel_integration.sh

# Configure Sentinel connection
vi /usr/bin/sentinel_integration.sh
# Edit: SENTINEL_MQTT_BROKER, DEVICE_ID, DEVICE_LOCATION

# Test connection
/usr/bin/sentinel_integration.sh test

# Install as cron job (checks every minute)
echo "* * * * * /usr/bin/sentinel_integration.sh monitor" >> /etc/crontabs/root

# Or run as persistent daemon
/usr/bin/sentinel_integration.sh daemon &
```

**MQTT Topics:**
- Motion events: `sentinel/motion/backyard`
- Floodlight commands: `sentinel/floodlight/wyze_floodlight_1/cmd`
- Floodlight state: `sentinel/floodlight/wyze_floodlight_1/state`
- Alarm snapshots: `sentinel/alarm/snapshot`

## 🔧 Setup Workflow

### Step 1: Flash Thingino Firmware
Follow instructions in [CAMERA_IMPLEMENTATION_COMPLETE.md](../../CAMERA_IMPLEMENTATION_COMPLETE.md) for SD card flashing.

### Step 2: Initial Thingino Configuration
```bash
# SSH into device (default password: root)
ssh root@192.168.1.101

# Set static IP (optional)
vi /etc/network/interfaces

# Update packages
opkg update
opkg install mosquitto-clients curl
```

### Step 3: Discover GPIO Pins
```bash
# Run discovery script
./gpio_discover.sh

# Note output:
# >>> FOUND FLOODLIGHT GPIO: 52 <<<
# >>> ACTIVITY DETECTED on GPIO 49: 0 -> 1 <<<
```

### Step 4: Install Control Scripts
```bash
# Install floodlight control
mv floodlight_control.sh /usr/sbin/floodlight_ctl
chmod +x /usr/sbin/floodlight_ctl

# Configure with discovered GPIOs
floodlight_ctl config set FLOODLIGHT_GPIO 52
floodlight_ctl config set PIR_SENSOR_GPIO 49

# Test
floodlight_ctl test
```

### Step 5: Enable Auto Mode (Optional)
```bash
# Configure auto mode settings
floodlight_ctl config set AUTO_MODE_TIMEOUT 300    # 5 minutes
floodlight_ctl config set LUX_THRESHOLD 100        # Turn on when dark

# Start auto mode
floodlight_ctl auto

# Check status
floodlight_ctl status
```

### Step 6: Integrate with Sentinel
```bash
# Install Sentinel integration
mv sentinel_integration.sh /usr/bin/sentinel_integration.sh
chmod +x /usr/bin/sentinel_integration.sh

# Edit configuration
vi /usr/bin/sentinel_integration.sh
# Set: SENTINEL_MQTT_BROKER=192.168.1.50
# Set: DEVICE_ID=wyze_floodlight_1
# Set: DEVICE_LOCATION=backyard

# Test MQTT connection
/usr/bin/sentinel_integration.sh test

# Enable monitoring (cron method)
echo "* * * * * /usr/bin/sentinel_integration.sh monitor" >> /etc/crontabs/root
/etc/init.d/cron restart
```

## 🎯 Sentinel Integration

### Adding Floodlight to Sentinel

**Option 1: MQTT Motion Sensor**
```json
// Add to Sentinel MQTT configuration
{
  "mqtt": {
    "sensors": [
      {
        "id": "floodlight_motion",
        "name": "Backyard Motion",
        "topic": "sentinel/motion/backyard",
        "type": "motion",
        "zone": 8
      }
    ]
  }
}
```

**Option 2: Virtual Relay**
Add floodlight as controllable relay in Sentinel web UI:
1. Go to Configuration → Relays
2. Add Virtual Relay:
   - Name: "Backyard Floodlight"
   - MQTT Topic: `sentinel/floodlight/wyze_floodlight_1/cmd`
   - ON Payload: `ON`
   - OFF Payload: `OFF`

**Option 3: Automation Rules**
Configure Sentinel to trigger floodlight on alarm:
```javascript
// In Sentinel automation config
{
  "trigger": "alarm_activated",
  "action": "mqtt_publish",
  "topic": "sentinel/floodlight/wyze_floodlight_1/cmd",
  "payload": "ON"
}
```

### Testing Integration

```bash
# From Sentinel device, test MQTT command:
mosquitto_pub -h 192.168.1.101 \
              -t "sentinel/floodlight/wyze_floodlight_1/cmd" \
              -m "ON"

# Monitor motion events:
mosquitto_sub -h 192.168.1.50 \
              -t "sentinel/motion/#" \
              -v

# Trigger test alarm in Sentinel and verify floodlight activates
```

## 📋 Configuration Files

### `/etc/floodlight.conf`
```bash
# Wyze Floodlight v1 Configuration

# GPIO Pin Assignments (update after gpio_discover.sh)
FLOODLIGHT_GPIO=52
PIR_SENSOR_GPIO=49

# Auto Mode Settings
AUTO_MODE_TIMEOUT=300  # seconds (5 minutes)
LUX_THRESHOLD=100      # turn on below this value

# MQTT Settings (for Sentinel integration)
MQTT_BROKER=192.168.1.50
MQTT_USER=sentinel
MQTT_PASS=your_password_here

# Enable/Disable Features
ENABLE_MQTT=1
ENABLE_AUTO_MODE=1
```

## 🔍 Troubleshooting

### GPIO Discovery Issues
**Problem:** `gpio_discover.sh` doesn't find GPIO pins

**Solutions:**
1. Check if GPIO subsystem exists:
   ```bash
   ls /sys/class/gpio/
   ```

2. Try manual GPIO export:
   ```bash
   for i in $(seq 40 70); do
     echo $i > /sys/class/gpio/export 2>/dev/null
   done
   ```

3. Check kernel logs:
   ```bash
   dmesg | grep -i gpio
   ```

4. Ask Thingino community with device info:
   ```bash
   cat /proc/cpuinfo
   cat /etc/os-release
   lsusb
   ```

### Floodlight Not Responding
**Problem:** `floodlight_ctl on` doesn't turn on LEDs

**Solutions:**
1. Verify GPIO pin is correct:
   ```bash
   echo 52 > /sys/class/gpio/export
   echo out > /sys/class/gpio/gpio52/direction
   echo 1 > /sys/class/gpio/gpio52/value
   # Try different pins: 48-63
   ```

2. Check if USB-controlled (not GPIO):
   ```bash
   lsusb
   # Look for LED controller device
   ```

3. Check for existing control utilities:
   ```bash
   find /usr -name "*light*" -o -name "*led*"
   ls /usr/sbin/
   ```

### PIR Sensor Not Detecting Motion
**Problem:** Motion detection doesn't work

**Solutions:**
1. Verify PIR GPIO with manual monitoring:
   ```bash
   echo 49 > /sys/class/gpio/export
   echo in > /sys/class/gpio/gpio49/direction
   
   # Monitor in loop
   while true; do
     cat /sys/class/gpio/gpio49/value
     sleep 0.5
   done
   ```

2. Check PIR power:
   ```bash
   # PIR may need 3.3V enable pin
   # Try enabling nearby GPIO pins
   ```

3. Test with different GPIO pins (40-55 range common for sensors)

### MQTT Connection Failed
**Problem:** Sentinel integration can't connect

**Solutions:**
1. Test MQTT manually:
   ```bash
   mosquitto_pub -h 192.168.1.50 -t test -m hello
   ```

2. Install mosquitto-clients if missing:
   ```bash
   opkg update
   opkg install mosquitto-clients
   ```

3. Check firewall on Sentinel:
   ```bash
   # On Sentinel device
   sudo ufw allow 1883/tcp
   ```

4. Verify credentials in `/usr/bin/sentinel_integration.sh`

## 🚀 Advanced Features

### Schedule-Based Lighting
Create cron job for automatic on/off times:
```bash
# Turn on at sunset (8 PM), off at sunrise (6 AM)
echo "0 20 * * * /usr/sbin/floodlight_ctl on" >> /etc/crontabs/root
echo "0 6 * * * /usr/sbin/floodlight_ctl off" >> /etc/crontabs/root
/etc/init.d/cron restart
```

### Brightness Control (if supported)
If floodlight supports PWM dimming:
```bash
# Add to floodlight_ctl
FLOODLIGHT_PWM="/sys/class/pwm/pwmchip0/pwm0"

# Set brightness (0-100%)
brightness_set() {
    local percent=$1
    local period=1000000  # 1ms period
    local duty=$((period * percent / 100))
    echo $duty > "$FLOODLIGHT_PWM/duty_cycle"
}
```

### Sunrise/Sunset Integration
Use astronomical calculations for smart scheduling:
```bash
# Install sunwait utility
opkg install sunwait

# Use in cron
0 * * * * /usr/bin/sunwait wait set && /usr/sbin/floodlight_ctl auto
```

## 📚 Additional Resources

- **Thingino Wiki:** https://github.com/themactep/thingino-firmware/wiki
- **Wyze Accessories:** https://github.com/themactep/thingino-firmware/wiki/Wyze-Accessories
- **Discord Community:** https://discord.gg/xDmqS944zr
- **Telegram Group:** https://t.me/thingino
- **Sentinel Docs:** [CAMERA_IMPLEMENTATION_COMPLETE.md](../../CAMERA_IMPLEMENTATION_COMPLETE.md)

## ⚠️ Important Notes

1. **GPIO pins are device-specific** - Discovery script helps find correct pins
2. **Floodlight may use USB control** - Check `lsusb` output if GPIO doesn't work
3. **Backup stock firmware** - Before flashing Thingino, backup original firmware
4. **Test incrementally** - Test each script independently before full integration
5. **Community support** - Join Discord for device-specific help

## 📝 License

MIT License - Free to use and modify for your Sentinel alarm system.

## 🤝 Contributing

If you successfully configure Wyze Floodlight v1:
1. Document your GPIO pin assignments
2. Share with Thingino community
3. Submit PR to update Wyze Accessories wiki
4. Help others with same hardware

---

**Version:** 1.0  
**Last Updated:** January 31, 2026  
**Tested On:** Wyze Floodlight v1 + Thingino (Ingenic T31)  
**Status:** Experimental - GPIO discovery required
