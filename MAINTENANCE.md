# Maintenance Guide: Sentinel-ESP

Guide for maintaining and supporting Sentinel-ESP alarm system firmware in production.

---

## Daily Operations

### Monitoring System Health

**Check MQTT Heartbeat**:
```bash
# Subscribe to heartbeat topic
mosquitto_sub -h broker.example.com -t 'sentinel/status/heartbeat' -u sentinel -P password

# Should receive message every 30 seconds:
# {"arm_state": 2, "zones": [...], "battery": 92}
```

**Monitor Web UI**:
- URL: `http://192.168.x.x`
- Check "Status" page for:
  - System state (Armed/Disarmed/Alarmed)
  - All zones showing correct state (secure/violated)
  - Last alarm time and zone
  - Network status (online/offline)

**Verify Logging**:
```bash
# Monitor ESP32 serial output
idf.py monitor /dev/ttyUSB0

# Look for these patterns:
# ✓ "Zone polling active"
# ✓ "MQTT connected"
# ✗ Any "ERROR" or "WARN" entries (investigate)
```

---

## Weekly Maintenance

### Test Alert Channels

**Telegram**:
```bash
# Force trigger test alert
# Via web UI: Configuration → Test Alert → Telegram

# Verify:
- Message received within 2 seconds
- Zone name correct
- Timestamp accurate
```

**Email**:
```bash
# Via web UI: Configuration → Test Alert → Email

# Verify:
- Email received in inbox (check spam folder)
- HTML formatting correct
- All zone details present
```

**MQTT Heartbeat**:
```bash
# Check last message timestamp
mosquitto_sub -h broker -t 'sentinel/status/heartbeat'

# If missing for >60s:
- Check network connectivity
- Verify MQTT credentials
- Check log output for connection errors
```

### Zone Testing

**Physical Test of Each Zone**:
```bash
# 1. Ensure system is DISARMED (safe mode)
# 2. For each zone:
#    - Trigger sensor (open door, wave motion, etc.)
#    - Verify zone state changes on web UI
#    - Check log: "Zone X triggered"
# 3. All zones should show correct state
```

**Relay Testing**:
```bash
# 1. Via web UI: Manual Relays
# 2. Toggle each relay individually:
#    - Relay 0 (Siren) → Should hear/see activation
#    - Relay 1 (Strobe) → Should see light
#    - Relay 2 (Lock) → Should hear click
# 3. All relays should respond immediately
```

---

## Monthly Maintenance

### Configuration Backup

**Create SPIFFS Backup**:
```bash
# Extract current configuration
esptool.py -p /dev/ttyUSB0 read_flash 0x110000 0x100000 spiffs_backup_$(date +%Y%m%d).bin

# Store safely (version control or cloud storage)
```

**Export Configuration JSON**:
```bash
# Via web UI: Configuration → Export
# Or directly from device:
# rsync user@192.168.x.x:/data/*.json ./backup/

# Files to backup:
- zones.json
- relays.json
- users.json
- config.json
- network.json
```

### Performance Review

**Check System Metrics**:
```bash
# Via web UI: System → Diagnostics
# Expected metrics:
- Zone poll latency: <5ms (32 zones)
- Memory usage: <150KB
- Uptime: >30 days (target)
- Network latency: <100ms to MQTT broker
```

**Log Analysis**:
```bash
# Export logs
idf.py monitor /dev/ttyUSB0 > logs_$(date +%Y%m%d).txt 2>&1 &

# Analyze for patterns:
grep "ERROR" logs_*.txt | sort | uniq -c | sort -rn
grep "WARN" logs_*.txt | sort | uniq -c | sort -rn

# Action items:
- If same error repeating → investigate root cause
- If memory warnings → check for leaks
```

### User Access Review

**Check User List**:
```bash
# Via web UI: Configuration → Users
# Verify:
- No unauthorized users added
- Inactive users disabled
- PINs haven't been reused
- Admin privileges correct
```

**Audit Log**:
```bash
# Check who disarmed system (if logging available)
# Review MQTT history for arm/disarm events
# Alert on unusual patterns (arm/disarm at odd hours)
```

---

## Quarterly Maintenance

### Firmware Update

**Check for New Version**:
```bash
# Compare current version with latest release
# Git repository or version file

# If update available:
```

**Test Update on Dev Board First**:
```bash
# Build new version
idf.py set-target esp32s3
idf.py build

# Flash to dev board
idf.py flash

# Test all functionality:
- Zones trigger correctly
- All alerts deliver
- MQTT connects
- Web UI responsive
- State persists after reboot
```

**Deploy to Production**:
```bash
# During maintenance window (no active alarms expected)
idf.py flash /dev/ttyUSB0 -b 921600

# Power cycle device
# Wait 30s for startup
# Verify on web UI and MQTT

# Rollback procedure if needed:
# Restore previous SPIFFS backup
# Reflash previous firmware
```

### Network Security Audit

**Check Certificate Expiration**:
```bash
# MQTT TLS certificate
echo | openssl s_client -servername broker.example.com -connect broker.example.com:8883 2>/dev/null | \
  openssl x509 -noout -dates

# If expires within 30 days:
- Renew certificate on broker
- Restart MQTT service
- Test connection (may need ESP32 reboot)
```

**Change Passwords** (if exposed or suspected):
```bash
# Via web UI: Configuration → Credentials
# Update:
- MQTT username/password
- Telegram bot token
- Noonlight API key
- Email SMTP password

# Test each after change to verify
```

### Database Housekeeping

**Clean Old Logs**:
```bash
# If storing logs on SD card:
# Keep last 3 months
find /mnt/sd_card/logs -name "*.log" -mtime +90 -delete

# Or compress old logs:
gzip /mnt/sd_card/logs/*.log.*
```

**Archive Alarm Events**:
```bash
# If tracking all zone events:
# Move events >6 months old to archive
# Delete after 1 year retention

# Helps prevent NVS/SPIFFS from filling up
```

---

## Incident Response

### System Won't Arm

**Diagnostic Steps**:
```
1. Check web UI: System State page
   - If "ALARMED": Must disarm first with correct PIN
   - If "EXITING": Wait for timer (may be stuck)
   - If "DISARMED": Proceed to step 2

2. Check zones: Configuration → Zones
   - Is every zone showing "secure" (1)?
   - Any zones showing "violated" (0)?
   - If yes: Fix physical sensors, then retry

3. Check MQTT: netstat or MQTT client
   - Is device connected to broker?
   - If not: Check network connectivity
   - Ping gateway: ping 192.168.1.1
   - Check logs: idf.py monitor | grep MQTT

4. If still won't arm:
   - Power cycle device
   - Check NVS not corrupted: erase_flash + reflash
```

### Zones Not Triggering

**Diagnostic Steps**:
```
1. Check GPIO configuration:
   - SSH to device: cat /proc/gpio
   - Verify zone GPIO matches zones.json
   - Expected GPIO range: 0-49

2. Physical test:
   - Manually trigger sensor (open door, wave motion)
   - Watch web UI: Zone state should change
   - If no change: Sensor broken or GPIO wrong

3. Check debounce:
   - If zone flickers: May need debounce tuning
   - Currently: 3 reads × 50ms = 150ms threshold
   - If sensor is bouncy: Increase threshold in code

4. Check arm mode:
   - Is zone included in current arm mode?
   - Configuration → Zones → Check arm_mode
   - "all" = always monitored
   - "stay" = only when armed in Stay mode
```

### Alert Delivery Failing

**Telegram Alert Not Received**:
```
1. Check bot token:
   curl "https://api.telegram.org/bot<TOKEN>/getMe"
   - Should return bot info (not 404)

2. Check chat ID:
   curl "https://api.telegram.org/bot<TOKEN>/getUpdates"
   - Bot must have at least one message in chat
   - Verify chat_id matches

3. Check network:
   ping api.telegram.org
   - Should respond (not timeout)

4. Check logs:
   idf.py monitor | grep -i telegram
   - Look for HTTP response code (200 = success)
   - 401 = auth failure
   - 403 = forbidden chat ID
   - 429 = rate limited

5. If still failing:
   - Re-generate bot token (webhook might be stale)
   - Ensure bot is member of chat
   - Add bot to chat: /start
```

**Email Not Received**:
```
1. Check SMTP credentials:
   Configuration → Email
   - Verify server (smtp.gmail.com, etc.)
   - Verify port (587 for TLS, 465 for SSL)
   - Verify username/password

2. Check firewall:
   telnet smtp.gmail.com 587
   - Should connect (not timeout)
   - If timeout: Firewall blocking port 587

3. Check spam folder:
   - Email might be there (retry from web UI)
   - Add sender to contacts to whitelist

4. Check logs:
   idf.py monitor | grep -i email
   - Look for SMTP connection attempts
   - Any "connection refused" = wrong port
   - Any "auth failed" = wrong credentials

5. Gmail-specific:
   - Enable "Less secure app access"
   - Or use "App password" (if 2FA enabled)
   - Check Gmail security alerts
```

**MQTT Not Publishing**:
```
1. Check broker connectivity:
   mosquitto_sub -h broker.example.com -t '#' -u sentinel -P password
   - Should connect and receive messages

2. Check device logs:
   idf.py monitor | grep -i MQTT
   - Look for "connected" or "connection failed"
   - Check retry backoff messages

3. If "connection refused":
   - Wrong hostname/port in config
   - Broker service not running
   - Firewall blocking connection

4. If "authentication failed":
   - Wrong username/password
   - User account disabled on broker

5. Restart MQTT connection:
   - Power cycle device (resets all connections)
   - Or trigger reconnect via API if available
```

### Memory Issues / Low RAM

**Symptoms**:
- Web UI becomes slow
- MQTT messages delayed
- Zones stop responding
- System reboots unexpectedly

**Diagnostic**:
```bash
# Check current memory usage
idf.py monitor | grep "heap"

# Should show:
# Heap free: ~80000 bytes (80KB available)

# If consistently <20KB:
- Memory leak in running task
- Check for unreleased buffers
- Profile with ESP-IDF tools
```

**Solutions**:
```bash
# 1. Reduce logging verbosity
#    CONFIG → System → log_level = 3 (ERROR only)

# 2. Increase task stack (if specific task leaking)
#    xTaskCreatePinnedToCore(..., 8192, ...)  # Increase

# 3. Disable unused features
#    CONFIG → Disable email/telegram if not used

# 4. Upgrade firmware
#    Newer builds may have memory optimizations
```

### Network Connectivity Lost

**Symptoms**:
- Web UI unreachable
- MQTT not publishing
- Alerts not delivering
- System boots but no IP address

**Diagnostic**:
```bash
# Check Ethernet driver
idf.py monitor | grep -i "ethernet\|eth"

# Expected:
# esp_eth: W5500 detected
# esp_netif: New IP event
# IP_EVENT_ETH_GOT_IP

# If not seeing these:
- W5500 not detecting (SPI issue)
- PHY not responding (hardware issue)
- Cable not connected
```

**Solutions**:
```bash
# 1. Check physical connection:
#    - Ethernet cable plugged in
#    - Switch is on and other devices connect
#    - Power cycle switch/router

# 2. Check ESP32 connection:
#    - USB power connected (indicator LED)
#    - Try different Ethernet port on router
#    - Check Router logs for DHCP events

# 3. Reset network:
#    - Power cycle ESP32 (unplug 10 seconds)
#    - Device should get new IP within 30s
#    - Check for IP on router: arp-scan or DHCP log

# 4. Reconfigure network:
#    - If static IP: Change to DHCP temporarily
#    - CONFIG → Network → use_dhcp = true
#    - Reflash: idf.py flash
#    - Should get DHCP IP within 30s
```

---

## Performance Optimization

### Reduce Alert Latency

**Current Timing**:
- Zone poll: 50ms
- Debounce: 150ms (3 × 50ms)
- Engine decision: <1ms
- Alert HTTP: 100-500ms (network dependent)
- **Total**: ~200-650ms

**To Improve**:
```c
// Reduce debounce (less noise immunity):
// In monitor.c: Change 3 reads to 2 reads
// Result: Debounce = 100ms, Total = ~150-550ms
// Risk: Increased false alarms from electrical noise

// Alternative: Use hardware debounce on GPIO:
// gpio_config_t cfg = {..., .pull_mode = GPIO_PULLUP, ...}
// Built-in debounce (if available on GPIO)
```

### Reduce Power Consumption

**Current Power Draw**:
- ESP32-S3: ~80mA (active)
- W5500: ~50mA (active)
- Relays off: ~130mA total
- With relay on: +300mA per relay

**To Reduce**:
```c
// Enable light sleep when not monitoring:
// esp_light_sleep_start()  // < 1mA sleep current
// Problem: Needs to wake on GPIO (zone trigger)
// Currently not implemented

// Turn off W5500 during night hours:
// eth_stop() / eth_start()
// Reduces to ~80mA, but breaks remote access
// Only useful if local-only operation desired
```

### Improve Web UI Responsiveness

**Current Response Time**: ~100ms

**To Improve**:
```
1. Compress JSON responses (gzip)
2. Cache static assets (CSS, JS)
3. Reduce database queries
4. Upgrade Mongoose (if newer version available)
```

---

## Common Configuration Changes

### Adding a New User

**Via Web UI**:
```
1. Configuration → Users → Add User
2. Enter: Name, PIN, Email, Phone
3. Select: Notification method, Admin flag
4. Click: Save
5. Verify in users.json
```

**Via JSON**:
```bash
# Edit /data/users.json
{
  "users": [
    {...existing users...},
    {
      "index": 1,
      "name": "NewUser",
      "pin": "4567",
      "phone": "555-1111",
      "email": "new@example.com",
      "notify": 2,
      "is_admin": false
    }
  ]
}

# Restart device: Power cycle or web UI restart
```

### Adding a New Zone

**Via Web UI**:
```
1. Configuration → Zones → Add Zone
2. Enter: Name, GPIO, Type
3. Select: Arm mode, Alert enabled
4. Click: Save
5. Verify in zones.json
```

**Important**: Must assign unique GPIO and map to physical sensor

### Changing Arm Delays

**In config.json**:
```json
{
  "system": {
    "arm_delay_seconds": 30,      // Time to exit before armed
    "entry_delay_seconds": 20,    // Time to enter PIN before alarm
    "siren_duration_seconds": 300 // Siren duration (0 = continuous)
  }
}
```

**After Change**:
```bash
# Power cycle device for changes to take effect
# Or via web UI: Restart → Save Configuration
```

---

## Backup & Restore

### Full System Backup

**What to Backup**:
```bash
# 1. SPIFFS (all configuration)
esptool.py -p /dev/ttyUSB0 read_flash 0x110000 0x100000 spiffs_backup.bin

# 2. NVS (system state)
esptool.py -p /dev/ttyUSB0 read_flash 0x9000 0x6000 nvs_backup.bin

# 3. Configuration JSON files
scp -r user@192.168.x.x:/data/*.json ./backup/

# 4. Current firmware
cp .platformio/build/esp32-s3-devkitc-1/firmware.bin ./firmware_backup.bin
```

### Full System Restore

**In Case of Corruption**:
```bash
# 1. Reflash SPIFFS
esptool.py -p /dev/ttyUSB0 write_flash 0x110000 spiffs_backup.bin

# 2. Reflash NVS
esptool.py -p /dev/ttyUSB0 write_flash 0x9000 nvs_backup.bin

# 3. Power cycle device
# Device should boot with all settings restored

# If SPIFFS corrupted and backup lost:
# Format SPIFFS: idf.py erase-flash
# Reflash firmware: idf.py flash
# Reconfigure from scratch via web UI
```

---

## Support Information

### Collecting Diagnostic Data

**When requesting support, provide**:
```
1. Serial output (last 100 lines):
   idf.py monitor > diagnostic.txt 2>&1 &
   # Let run for 5 minutes during issue occurrence
   
2. Web UI status screenshot:
   - System State page
   - Zone list
   - Last alert info
   
3. Configuration export:
   - zones.json
   - config.json (with credentials redacted)
   - network.json
   
4. Timing of issue:
   - When did it start?
   - What was system doing?
   - Any recent changes?
   
5. Hardware info:
   - ESP32 variant (ESP32-S3-DevKitC-1?)
   - W5500 MAC address
   - Firmware version
```

### Log Levels for Troubleshooting

| Level | Use | Command |
|-------|-----|---------|
| CRITICAL | Unrecoverable errors only | `log_level: 4` |
| ERROR | Errors + critical | `log_level: 3` |
| WARN | Standard production (default) | `log_level: 2` |
| INFO | Operation events + warnings | `log_level: 1` |
| DEBUG | Everything (development only) | `log_level: 0` |

**To Change**:
```json
{
  "system": {
    "log_level": 0  // DEBUG during troubleshooting
  }
}
```

---

## Disaster Recovery

### Catastrophic Failure (Won't Boot)

**Recovery Steps**:
```bash
# 1. Full erase
esptool.py -p /dev/ttyUSB0 erase_flash

# 2. Reflash bootloader
idf.py build
idf.py flash --bootloader-only

# 3. Reflash full firmware
idf.py flash

# 4. Device boots with defaults
# 5. Restore configuration from backup:
esptool.py -p /dev/ttyUSB0 write_flash 0x110000 spiffs_backup.bin

# 6. Power cycle to load config
```

### Lost Configuration (No Backup)

**Recovery**:
```bash
# 1. Device has default zones/users from source code
# 2. SSH to device and manually recreate config:
#    scp zones.json user@device:/data/zones.json

# Or reconfigure via web UI:
#    http://device.ip/config
#    - Define zones (match your physical setup)
#    - Define users (set PINs)
#    - Set network/MQTT/Telegram settings

# 3. Test all zones before putting system in service
```

---

## Regulatory Compliance

### Testing & Certification

**If selling/distributing system**:
- FCC (RF emissions): W5500 SPI interface
- UL/CE (Safety): Electrical safety, EMC
- UL2089 (Home Security): Professional monitoring interface

**Documentation Required**:
- Bill of materials (BOM)
- Schematic diagrams
- Test reports (emissions, immunity, safety)
- User manual
- Installation guide

---

## End-of-Life Support

### Firmware Security Updates

**Receiving Updates**:
1. Monitor GitHub releases or mailing list
2. Critical security updates available for 5 years
3. Standard bug fixes for 7 years
4. New feature development through project lifecycle

### Decommissioning

**Before Retiring System**:
```bash
# 1. Export current configuration
scp user@device:/data/*.json ./archive/

# 2. Download firmware version used
# 3. Create system backup (full SPIFFS + NVS)
esptool.py read_flash 0x9000 0xf7000 full_backup.bin

# 4. Securely erase sensitive data
# 5. Document configuration for historical records
# 6. Remove from network and storage
```

---

**Maintenance Guide Version**: 1.0  
**Last Updated**: 2024  
**Scope**: Sentinel-ESP Production Support

