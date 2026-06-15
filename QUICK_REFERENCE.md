# Sentinel-ESP Quick Reference Card

Print this page for quick access during development and maintenance.

---

## Build & Flash Commands

```bash
# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py flash

# Monitor serial
idf.py monitor /dev/ttyUSB0

# Clean build
idf.py fullclean && idf.py build

# Erase and reflash (factory reset)
idf.py erase-flash && idf.py flash
```

---

## GPIO Pin Map

| Pin | Function | Notes |
|-----|----------|-------|
| 2 | Relay 0 (Siren) | Primary alarm output |
| 4 | Relay 1 (Strobe) | Also W5500 interrupt |
| 5 | Relay 2 (Lock) | Also W5500 reset |
| 13 | Relay 3 | Also SPI MISO |
| 15 | Relay 4 | - |
| 18 | Relay 5 | - |
| 19 | Relay 6 | - |
| 21 | Relay 7 | - |
| 8 | I2C SDA | Zone expansion |
| 9 | I2C SCL | Zone expansion |
| 11 | SPI MOSI | W5500 |
| 12 | SPI SCLK | W5500 |
| 10 | SPI CS | W5500 |
| 3 | RF RX | 433MHz receiver |

**Available for zones**: 0, 1, 6, 7, 14, 16, 17, 26-49 (adjust per network usage)

---

## Configuration JSON Templates

### zones.json
```json
{
  "zones": [
    {
      "index": 0,
      "name": "Front Door",
      "type": "door",
      "pin": 32,
      "arm_mode": "all",
      "is_alert_zone": true
    }
  ]
}
```

### config.json (Minimal)
```json
{
  "system": {
    "master_pin": "1234",
    "arm_delay_seconds": 30,
    "entry_delay_seconds": 20
  },
  "mqtt": {
    "broker": "mqtt.example.com",
    "port": 8883,
    "username": "sentinel",
    "password": "***"
  }
}
```

---

## Security Components

### Password Hashing
- **Algorithm:** PBKDF2-SHA256 (10,000 iterations)
- **Salt:** 256-bit random per user
- **Storage:** 137-byte hex string in `pin_hash` field

### Session Tokens
- **Format:** JWT (HMAC-SHA256)
- **Expiration:** 1 hour
- **Storage:** NVS (secret key) + RAM (8 active sessions)

### Audit Log
- **Entries:** 100 (circular buffer)
- **File:** `/spiffs/audit.log`
- **API:** `GET /api/audit`

---

## Common Log Messages

| Message | Meaning | Action |
|---------|---------|--------|
| `Zone polling active` | Normal operation | ✓ OK |
| `MQTT connected` | Broker online | ✓ OK |
| `Ethernet connected` | W5500 online | ✓ OK |
| `Zone X triggered` | Sensor activated | Check sensor |
| `Invalid GPIO X` | Bad config | Fix zones.json |
| `MQTT retry 1/4` | Connection failed | Check broker |
| `Brute force attempt` | 5 failed PINs | Lockout 60s |
| `ERROR: ...` | Serious problem | See full log |

---

## State Machine States

| State | Value | Meaning |
|-------|-------|---------|
| DISARMED | 0 | System off, no monitoring |
| EXITING | 1 | Countdown to armed (exit delay) |
| ARMED | 2 | Monitoring active |
| ENTRY | 3 | Alarm triggered, waiting for PIN (entry delay) |
| ALARMED | 4 | Sirens active, alerts sent |

---

## Zone Types

- **door** - Door contact sensor
- **window** - Window contact sensor
- **motion** - PIR motion detector
- **glass_break** - Acoustic glass break
- **fire** - Smoke detector (24/7)
- **smoke** - Additional smoke sensor
- **panic** - Panic button (24/7)
- **medical** - Medical alert (24/7)
- **temp** - Temperature sensor
- **water** - Water leak detector

---

## Relay Types

- **alarm** - Primary siren
- **siren** - Secondary siren
- **strobe** - Visual alarm
- **chime** - Entry chime
- **bell** - Phone line alert
- **light** - Outdoor light
- **door_lock** - Electronic lock
- **gate** - Gate/barrier

---

## REST API Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/status` | GET | System state |
| `/zones` | GET | Zone list |
| `/config` | GET/POST | Configuration |
| `/arm` | POST | Arm system |
| `/disarm` | POST | Disarm (needs PIN) |
| `/relay/:id/toggle` | POST | Activate relay |

---

## MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `sentinel/status/heartbeat` | ← PUB | Status every 30s |
| `sentinel/state/arm_state` | ← PUB | State changes |
| `sentinel/alarm/triggered` | ← PUB | Zone events |
| `sentinel/alarm/set` | → SUB | Remote commands |

---

## Logging Levels

| Level | Value | Display | Use Case |
|-------|-------|---------|----------|
| DEBUG | 0 | All | Development |
| INFO | 1 | INFO, WARN, ERROR | Detailed |
| WARN | 2 | WARN, ERROR | Production (default) |
| ERROR | 3 | ERROR only | Minimal |
| CRITICAL | 4 | CRITICAL | Emergency |

Set in config.json: `"log_level": 2`

---

## Emergency Procedures

### System Won't Arm
```bash
1. Check zones: All must be "secure" (1)
2. Web UI: Configuration → Zones
3. Fix any "violated" zones
4. Retry arm
```

### Zones Don't Trigger
```bash
1. Verify GPIO in zones.json
2. Physical test: Trigger sensor
3. Check web UI: Zone state should change
4. If no change: Check GPIO/sensor
```

### MQTT Not Connecting
```bash
1. Check broker hostname in config
2. Verify MQTT port (8883 for TLS)
3. Test connectivity: telnet broker.com 8883
4. Verify username/password
```

### Device Won't Boot
```bash
1. Check USB power (LED should light)
2. Check serial output (9600 baud)
3. Try: idf.py erase-flash && idf.py flash
4. Last resort: Full reflash from scratch
```

---

## Performance Targets

| Metric | Target | Measured |
|--------|--------|----------|
| Zone poll latency | <50ms | ~5ms |
| Debounce delay | ~150ms | 3 × 50ms |
| Alarm response | <100ms | ~20ms |
| Alert HTTP | <1s | 100-500ms |
| Memory (DRAM) | <150KB | ~120KB |
| Uptime | 30+ days | Current |

---

## Security Checklist

- [ ] Master PIN changed from default (1234)
- [ ] MQTT password set (not empty)
- [ ] Telegram bot token valid
- [ ] Email credentials correct
- [ ] Network IP not public-facing
- [ ] Firewall blocks port 80/443 from outside
- [ ] User PINs are unique
- [ ] Admin users limited
- [ ] Backup created (monthly)
- [ ] Logs monitored for errors

---

## Troubleshooting Flowchart

```
System Down?
├─ No power?
│  └─ Check USB cable and power supply
├─ Won't boot?
│  └─ Try erase-flash and reflash
└─ Boots but not responding?
   ├─ Check web UI: http://device.ip
   ├─ Check MQTT: mosquitto_sub -t '#'
   └─ Check serial: idf.py monitor

Zones not triggering?
├─ All zones showing "secure" on web UI?
│  ├─ YES: Physical test sensor
│  └─ NO: Check GPIO and wiring
├─ Web UI shows correct state?
│  ├─ YES: Zone working, check configuration
│  └─ NO: GPIO configuration error

Alerts not delivered?
├─ Check Telegram: token and chat_id correct?
├─ Check Email: SMTP server and credentials?
├─ Check Network: Device online (MQTT connected)?
└─ Check Logs: idf.py monitor | grep -i alert
```

---

## File Locations

**Configuration**:
- Zones: `/data/zones.json`
- Config: `/data/config.json`
- Users: `/data/users.json`
- Network: `/data/network.json`

**Source Code**:
- Main: `main/main.c`
- Engine: `components/engine/engine.c`
- Networking: `components/comms/net.c`
- Hardware: `components/hardware/hal_esp32.c`

**Build Output**:
- Firmware: `build/app.bin`
- Logs: `build/*.log`

---

## Key Files to Know

| File | Purpose | Edit If |
|------|---------|---------|
| zones.json | Zone definitions | Adding/changing sensors |
| config.json | System settings | Tuning delays, credentials |
| hal_esp32.c | GPIO control | Adding new hardware |
| engine.c | State machine | Logic changes |
| storage_mgr.c | Config loading | JSON schema changes |

---

## Useful Commands

```bash
# Copy config from device
scp user@device:/data/zones.json ./

# Monitor specific topic
mosquitto_sub -h broker -t 'sentinel/status/+' -u user -P pass

# Force state change (dangerous!)
mosquitto_pub -h broker -t 'sentinel/alarm/set' -m '{"action":"arm"}' -u user -P pass

# Backup SPIFFS
esptool.py read_flash 0x110000 0x100000 backup.bin

# Restore SPIFFS
esptool.py write_flash 0x110000 backup.bin

# Check device memory
idf.py monitor | grep -i heap

# View compile errors clearly
idf.py build 2>&1 | grep -E "error:|warning:"
```

---

## Documentation Quick Links

| What You Need | Read This |
|---------------|-----------|
| Project overview | README.md |
| System design | ARCHITECTURE.md |
| Configuration | CONFIG.md |
| What changed | IMPROVEMENTS.md |
| How to maintain | MAINTENANCE.md |
| Function reference | Individual .h files |

---

## External Resources

| Resource | Purpose | URL |
|----------|---------|-----|
| ESP-IDF Docs | Framework reference | https://docs.espressif.com |
| FreeRTOS Docs | RTOS reference | https://www.freertos.org |
| Mongoose | HTTP server library | https://github.com/cesanta/mongoose |
| KC868-A8V3 | Hardware manual | Manufacturer website |

---

## Support Contacts

| Issue | Contact |
|-------|---------|
| Hardware failure | [Manufacturer support] |
| Firmware bug | [Project maintainer] |
| MQTT issues | [MQTT broker admin] |
| Monitoring service | [Noonlight support] |

---

## Emergency Numbers

Keep this posted near the alarm system:

```
Police:        911
Fire:          911
Medical:       911
System Admin:  _______________
Monitoring:    _______________
Manufacturer:  _______________
```

---

## Abbreviations

| Term | Meaning |
|------|---------|
| GPIO | General Purpose Input/Output |
| I2C | Inter-Integrated Circuit (two-wire bus) |
| SPI | Serial Peripheral Interface (four-wire bus) |
| MQTT | Message Queuing Telemetry Transport |
| RTC | Real-Time Clock |
| NVS | Non-Volatile Storage (persistent memory) |
| SPIFFS | SPI Flash File System |
| TLS | Transport Layer Security (encryption) |
| FreeRTOS | Free Real-Time Operating System |
| ADC | Analog-to-Digital Converter |
| PWM | Pulse Width Modulation |
| RMT | Remote Control Module (for RF) |
| PIR | Passive Infrared (motion sensor) |

---

**Quick Reference Version**: 1.0  
**Last Updated**: 2024  
**Print & Post Near System**: YES ✓

