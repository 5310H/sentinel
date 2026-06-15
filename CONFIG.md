# Sentinel-ESP Configuration Guide

## Quick Start

### 1. Initial Setup
```bash
# Clone and build
cd /path/to/sentinel-esp
idf.py set-target esp32s3
idf.py build
idf.py flash

# Monitor output
idf.py monitor /dev/ttyUSB0
```

### 2. Access Web UI
- Device IP: Check OLED display or router (look for 192.168.x.x)
- URL: `http://192.168.x.x`
- Default login: Any user with PIN (configured in users.json)

### 3. Configure Zones
Visit **Config** page → **Zones** to set up:
- GPIO pins for each zone
- Zone types (Door, Window, Motion, etc.)
- Arm modes (Away, Stay, Night)
- Alert sensitivity

---

## Configuration Files

### storage_mgr.h Constants
```c
#define MAX_ZONES    32
#define MAX_RELAYS   8
#define MAX_USERS    16
#define MAX_CONFIG_NAME_LEN 64
#define MAX_ZONE_NAME_LEN 32
```

All configuration files stored in `/data/` (SPIFFS):

### 1. zones.json
Defines all door/window/motion/fire sensors

**Required Fields**:
- `index`: 0-31 (unique identifier)
- `name`: Display name (e.g., "Front Door")
- `type`: Zone type (see zone_type_t enum)
- `pin`: GPIO number (0-49 valid for ESP32-S3)
- `arm_mode`: "all" (armed in all modes), "perimeter", "interior", "stay", "night"
- `is_alert_zone`: true/false (whether to trigger alarm)

**Example**:
```json
{
  "zones": [
    {
      "index": 0,
      "name": "Front Door",
      "type": "door",
      "description": "Front entrance contact sensor",
      "pin": 32,
      "arm_mode": "all",
      "is_alert_zone": true,
      "debounce_ms": 100
    },
    {
      "index": 1,
      "name": "Living Room Motion",
      "type": "motion",
      "description": "Main floor occupancy detector",
      "pin": 33,
      "arm_mode": "stay",
      "is_alert_zone": true,
      "debounce_ms": 200
    },
    {
      "index": 2,
      "name": "Master Bedroom Fire",
      "type": "fire",
      "description": "Smoke detector",
      "pin": 34,
      "arm_mode": "all",
      "is_alert_zone": true,
      "debounce_ms": 100
    }
  ]
}
```

### 2. relays.json
Controls output devices (sirens, lights, locks, etc.)

**Required Fields**:
- `index`: 0-7 (KC868-A8V3 has 8 relays)
- `name`: Display name
- `type`: Relay type (see relay_type_t enum)
- `gpio_pin`: Output GPIO (ignored - hardcoded to KC868-A8V3 pins)
- `normally_open`: true/false (relay behavior)

**Example**:
```json
{
  "relays": [
    {
      "index": 0,
      "name": "Primary Siren",
      "type": "alarm",
      "description": "Main 120dB alarm siren",
      "gpio_pin": 2,
      "normally_open": true,
      "pulse_duration_ms": 0
    },
    {
      "index": 1,
      "name": "Strobe Light",
      "type": "strobe",
      "description": "Visual alarm - strobe light",
      "gpio_pin": 4,
      "normally_open": true,
      "pulse_duration_ms": 500
    },
    {
      "index": 2,
      "name": "Front Door Lock",
      "type": "door_lock",
      "description": "Electronic door lock control",
      "gpio_pin": 5,
      "normally_open": false,
      "pulse_duration_ms": 3000
    }
  ]
}
```

### 3. users.json
System users with PINs and notification preferences

**Fields**:
- `index`: 0-15 (0 reserved for master)
- `name`: Username
- `pin`: Numeric PIN (4-8 digits)
- `phone`: Phone number for SMS
- `email`: Email for alerts
- `notify`: Notification method (1=None, 2=Email, 3=Telegram, 4=Noonlight, 5=Multiple)
- `is_admin`: true/false (admin can modify config)

**Example**:
```json
{
  "users": [
    {
      "index": 0,
      "name": "Master",
      "pin": "1234",
      "phone": "555-0000",
      "email": "admin@home.com",
      "notify": 2,
      "is_admin": true
    },
    {
      "index": 1,
      "name": "Alice",
      "pin": "5678",
      "phone": "555-1111",
      "email": "alice@home.com",
      "notify": 2,
      "is_admin": false
    },
    {
      "index": 2,
      "name": "Bob",
      "pin": "9999",
      "phone": "555-2222",
      "email": "bob@home.com",
      "notify": 3,
      "is_admin": false
    }
  ]
}
```

### 4. config.json
System settings, credentials, and preferences

**Main Sections**:

#### System Settings
```json
{
  "system": {
    "location": "Home Security System",
    "time_format": "24h",
    "date_format": "YYYY-MM-DD",
    "arm_delay_seconds": 30,
    "entry_delay_seconds": 20,
    "siren_duration_seconds": 300,
    "master_pin": "1234",
    "log_level": 2
  }
}
```

- `arm_delay_seconds`: Time before armed mode activates (exit delay)
- `entry_delay_seconds`: Time to enter PIN before alarm triggers
- `siren_duration_seconds`: How long siren blares (0 = indefinite until disarmed)
- `log_level`: 0=DEBUG, 1=INFO, 2=WARN (default), 3=ERROR, 4=CRITICAL

#### Network
```json
{
  "network": {
    "use_dhcp": true,
    "static_ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "netmask": "255.255.255.0",
    "dns": "8.8.8.8"
  }
}
```

#### MQTT
```json
{
  "mqtt": {
    "enabled": true,
    "broker": "mqtt.example.com",
    "port": 8883,
    "username": "sentinel",
    "password": "secure_password",
    "use_tls": true,
    "heartbeat_interval_seconds": 30
  }
}
```

#### Email (SMTP)
```json
{
  "email": {
    "enabled": true,
    "smtp_server": "smtp.gmail.com",
    "smtp_port": 587,
    "username": "alert@example.com",
    "password": "app_specific_password",
    "admin_email": "admin@home.com",
    "use_tls": true
  }
}
```

#### Telegram
```json
{
  "telegram": {
    "enabled": true,
    "bot_token": "123456789:ABCDefGHIjklmnoPQRstuvWXyz",
    "chat_id": "1234567890"
  }
}
```

#### Noonlight (Professional Monitoring)
```json
{
  "noonlight": {
    "enabled": false,
    "api_key": "your_api_key",
    "account_id": "your_account_id",
    "api_url": "https://api.noonlight.com"
  }
}
```

### 5. network.json
Network interface configuration

```json
{
  "ethernet": {
    "enabled": true,
    "mac_address": "AA:BB:CC:DD:EE:FF",
    "spi_freq": 25000000,
    "spi_mode": 0
  },
  "wifi": {
    "enabled": false,
    "ssid": "WiFi_Network",
    "password": "wifi_password"
  },
  "mdns": {
    "enabled": true,
    "hostname": "sentinel"
  }
}
```

---

## GPIO Pin Configuration

### ESP32-S3 to KC868-A8V3 Mapping

#### Relay Outputs (Fixed Mapping)
```c
Relay 0 → GPIO 2   (Pin 1 on KC868-A8V3 relay header)
Relay 1 → GPIO 4   (Pin 2)
Relay 2 → GPIO 5   (Pin 3)
Relay 3 → GPIO 13  (Pin 4)
Relay 4 → GPIO 15  (Pin 5)
Relay 5 → GPIO 18  (Pin 6)
Relay 6 → GPIO 19  (Pin 7)
Relay 7 → GPIO 21  (Pin 8)
```

#### I2C Bus (Zone Expansion)
```c
SDA → GPIO 8  (Pin D+ on KC868-A8V3 I2C header)
SCL → GPIO 9  (Pin C+ on KC868-A8V3 I2C header)
```

#### SPI Bus (W5500 Ethernet)
```c
MOSI    → GPIO 11  (SI pin on W5500)
MISO    → GPIO 13  (SO pin on W5500)  [Note: Also used as Relay 3]
SCLK    → GPIO 12  (SCK pin on W5500)
CS      → GPIO 10  (SS pin on W5500)
INT     → GPIO 4   (INT pin on W5500)  [Note: Also used as Relay 1]
RST     → GPIO 5   (RST pin on W5500)  [Note: Also used as Relay 2]
```

#### Zone Inputs (GPIO Available)
Valid GPIO for zone inputs: **0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49**

Recommended zone GPIO (avoid conflicts):
- **32, 33, 34, 35, 36, 37, 38, 39** (ADC-capable, usually not needed for zones)
- **42, 43, 44, 45, 46, 47, 48, 49** (Upper range, fewer conflicts)

**Avoid These GPIO**:
- 0, 1: Reserved for USB-Serial
- 10, 12, 13: SPI bus (W5500)
- 2, 4, 5, 18, 19, 21: Already used for relays
- 8, 9: I2C bus (zone expansion)

#### RF Receiver (433MHz Wireless Zones)
```c
RX → GPIO 3 (RMT peripheral, Rx mode)
TX → GPIO 6 (RMT peripheral, Tx mode - not used)
```

---

## Zone Type Reference

| Type | Use Case | Example | Arm Mode | Notes |
|------|----------|---------|----------|-------|
| **door** | Door contact sensor | Front door | All | Highest priority |
| **window** | Window contact | Bedroom windows | All | - |
| **motion** | PIR motion detector | Living room | Stay-only | Disable in night mode |
| **glass_break** | Acoustic sensor | Large windows | All | May require tuning |
| **fire** | Smoke detector | Kitchen | All | Always active (24/7) |
| **smoke** | Additional smoke | Bedroom | All | Supplement to fire |
| **panic** | Panic button | Keypad | All | 24/7 active |
| **medical** | Medical alert | Bedside | All | 24/7 active |
| **temp** | Temperature sensor | Freezer | Stay | Alerts if too warm |
| **water** | Water leak detector | Basement | All | Alerts if water detected |

---

## Alert Routing Examples

### Example 1: Fire Alert (24/7)
```
Zone: Kitchen Smoke Detector (ZONE_TYPE_FIRE)
    ↓
dispatcher_alert(zone, ALERT_FIRE)
    ↓
Is config.notify_fire enabled? Yes
    ↓
Route to ALL:
  - Email: smtp_alert_all_contacts()
  - Telegram: telegram_send_alert()
  - Noonlight: noonlight_create_alarm(SLOT_FIRE)
```

### Example 2: Intruder (Armed Only)
```
Zone: Front Door (ZONE_TYPE_DOOR) [System ARMED]
    ↓
engine_trigger_alarm()
    ↓
dispatcher_alert(zone, ALERT_POLICE)
    ↓
Is config.notify_police enabled? Yes
    ↓
Route to ALL:
  - Email: smtp_alert_all_contacts()
  - Telegram: telegram_send_alert()
  - Noonlight: noonlight_create_alarm(SLOT_POLICE)
```

### Example 3: Motion Ignored (Stay Mode)
```
Zone: Living Room Motion (ZONE_TYPE_MOTION)
System: STAY mode (interior disabled)
    ↓
monitor_process_event(motion_zone)
    ↓
if (arm_mode != STAY) { trigger alarm }
    ↓
No alarm triggered
```

---

## Building & Flashing

### Development Build
```bash
cd /home/kjgerhart/sentinel-esp

# Configure
idf.py set-target esp32s3
idf.py menuconfig

# Build with optimizations for debugging
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor /dev/ttyUSB0
```

### Production Build
```bash
# Optimize for size/speed
idf.py set-target esp32s3
idf.py build -DLOG_LEVEL_WARNING
idf.py flash -b 921600

# Power cycle device
```

### Linux Testing
```bash
# Build mock version for testing
cd /home/kjgerhart/sentinel-esp/test_linux
make clean
make

# Run tests
./sentinel_test
```

---

## Troubleshooting

### Issue: "Zone triggers but alarm doesn't fire"
**Check**:
1. Zone is configured correctly in zones.json
2. GPIO pin is valid (0-49)
3. System is actually ARMED (check web UI)
4. Arm mode includes this zone ("all" or matching mode)
5. `is_alert_zone` is true in config

**Debug**:
```
logging_set_level(LOG_LEVEL_DEBUG);
// Watch monitor_task output for zone events
// Should see: "Zone 0 state change: secure → violated"
```

### Issue: "MQTT won't connect"
**Check**:
1. Network is up (ping gateway)
2. MQTT broker hostname resolves (check logs)
3. Credentials correct in config.json
4. Firewall allows port 8883 (TLS) or 1883 (unencrypted)
5. TLS certificates valid

**Debug**:
```
idf.py monitor | grep MQTT
// Should see connection attempts and retry backoff
```

### Issue: "Telegram alerts not working"
**Check**:
1. Bot token is valid in config.json
2. Chat ID is correct (not group ID)
3. Bot is member of the chat
4. Network is up (check HTTP requests)
5. Telegram API not rate-limited (10 msg/sec)

**Debug**:
```
// Check HTTP response codes in logs
LOG_INFO("Telegram response: %d", http_status);
```

### Issue: "Web UI not accessible"
**Check**:
1. Device IP on OLED display or router
2. Firewall allows port 80 on local network
3. Mongoose web server started (check logs)
4. Network is up (ping from computer)

**Debug**:
```
curl http://192.168.x.x/status
// Should return JSON: {"arm_state": 0, "zones": [...]}
```

---

## Performance Tuning

### Reduce Entry/Exit Delay (for fast testing)
```json
{
  "system": {
    "arm_delay_seconds": 5,
    "entry_delay_seconds": 5,
    "siren_duration_seconds": 10
  }
}
```

### Increase Log Level (reduce CPU)
```json
{
  "system": {
    "log_level": 3  // ERROR only
  }
}
```

### Reduce MQTT Heartbeat (if bandwidth limited)
```json
{
  "mqtt": {
    "heartbeat_interval_seconds": 60
  }
}
```

### Disable Unused Alert Channels
```json
{
  "telegram": {"enabled": false},
  "email": {"enabled": false},
  "noonlight": {"enabled": false}
}
```

---

## Factory Reset

### Reset to Defaults
```bash
# Erase NVS (will reset all state/config)
idf.py erase-flash
idf.py flash

# Device will boot with default zones/users/config
```

### Backup Current Configuration
```bash
# Extract SPIFFS partition
idf.py read_flash 0x110000 0x100000 spiffs_backup.bin

# Restore if needed
idf.py write_flash 0x110000 spiffs_backup.bin
```

---

## Next Steps

- See **[ARCHITECTURE.md](ARCHITECTURE.md)** for system design details
- See **[API.md](API.md)** for REST endpoint documentation
- See **[MQTT.md](MQTT.md)** for message broker topics

