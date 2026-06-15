# RF 433MHz Wireless Sensor Setup and Usage

## Overview

The Sentinel-ESP system supports 433MHz wireless sensors using the ESP32-S3 RMT (Remote Control) peripheral for RF signal decoding. This enables wireless door/window sensors, motion detectors, key fobs, and panic buttons to integrate seamlessly with the alarm system.

## Hardware Configuration

### RF Receiver Module

**Supported Receivers:**
- Generic 433MHz superheterodyne receiver modules
- XY-MK-5V / RXB6 (recommended for better sensitivity)
- WL102-341 / FS1000A receiver
- Any ASK/OOK 433MHz receiver with data output pin

**Connection:**
```
RF Receiver → ESP32-S3
─────────────────────
VCC (5V)    → 5V
GND         → GND
DATA        → GPIO 4 (configurable in rf_driver.c)
```

**GPIO Configuration:**
- Default: GPIO 4 (defined as `RF_RECEIVER_GPIO` in rf_driver.c)
- Can be changed to any available GPIO pin
- Uses RMT peripheral in RX mode (no PWM/LED conflicts)

### Hardware Setup

```
┌──────────────────────┐
│   433MHz Receiver    │
│  ┌────┐  ┌────┐      │
│  │ANT │  │DATA├──────┼──> GPIO 4 (ESP32-S3)
│  └────┘  └────┘      │
│   VCC  GND           │
└───┬────┬─────────────┘
    │    │
    5V  GND (ESP32)
```

**Antenna:**
- Wire length: 17.3 cm (λ/4 for 433.92 MHz)
- Position: Vertical orientation for best reception
- Keep away from metal enclosures and high-power lines

## Supported RF Protocols

### PT2262 / EV1527 (Default)

The system is configured for PT2262/EV1527 encoding, commonly used in:
- Door/window sensors
- PIR motion detectors
- Remote key fobs
- Panic buttons
- Smoke/CO detectors

**Protocol Specifications:**
```
Encoding: OOK (On-Off Keying)
Frequency: 433.92 MHz
Data bits: 24 bits (20-bit address + 4-bit data)
Sync pulse: 300μs high, 9300μs low
Bit '0': 300μs high, 900μs low
Bit '1': 900μs high, 300μs low
```

**Timing Diagram:**
```
        Sync    Bit '0'   Bit '1'   ...
         |        |          |
    ____┐        ┐          ┐┐┐      
        │300μs   │300μs     │900μs   
    ────┴────────┴──────────┴───────
         9300μs   900μs      300μs
```

### Other Protocols

To support different RF protocols (SC2260, HT6P20B, etc.), modify these values in `rf_driver.c`:

```c
// Example: SC2260 protocol
#define RF_SYNC_HIGH_US          500
#define RF_SYNC_LOW_US           9500
#define RF_BIT_SHORT_US          500
#define RF_BIT_LONG_US           1500
#define RF_CODE_BIT_COUNT        24

// Example: HT6P20B protocol
#define RF_SYNC_HIGH_US          450
#define RF_SYNC_LOW_US           9000
#define RF_BIT_SHORT_US          450
#define RF_BIT_LONG_US           1350
#define RF_CODE_BIT_COUNT        20
```

## Software Configuration

### 1. RF Driver Initialization

The RF driver is automatically initialized in `main.c`:

```c
// In app_main()
rf_driver_init();
```

This:
- Configures RMT peripheral for RX on GPIO 4
- Creates parser task (priority 10, 3KB stack)
- Starts receiving RF signals
- Logs: `RF Driver Initialized`

### 2. Learning RF Codes

**Method 1: Serial Monitor (Recommended)**

```bash
idf.py monitor
```

Activate your RF sensor (open door, press button) and watch for:
```
I (12345) RF: Received RF Code: 0xABCD12
```

**Method 2: Web API Status**

```bash
curl http://192.168.1.x/status
```

Response includes last RF code:
```json
{
  "rf": {
    "last": "ABCD12"
  }
}
```

**Method 3: Web UI**

Open `http://192.168.1.x/tester.html` and watch the RF code field update in real-time.

### 3. Assigning RF Codes to Users

Edit `/data/users.json` or use the web API:

```json
{
  "users": [
    {
      "name": "John",
      "pin": "1234",
      "totp_secret": "",
      "rf": "ABCD12",
      "role": "admin"
    },
    {
      "name": "Wireless Fob",
      "pin": "",
      "totp_secret": "",
      "rf": "EF5678",
      "role": "user"
    }
  ]
}
```

**Via API:**
```bash
curl -X POST http://192.168.1.x/api/users/add \
  -H "Content-Type: application/json" \
  -d '{"name":"Keyfob","rf":"ABCD12","role":"user"}'
```

**Via storage_mgr.c:**
```c
storage_add_rf_user("Keyfob", "ABCD12");
```

### 4. Using RF for Arming/Disarming

**Current Implementation:**
- RF codes are stored in `last_rf_code` global variable
- Web API `/status` endpoint exposes last received code
- Users can be assigned RF codes for identification

**To Enable RF Control (Requires Engine Integration):**

Add to `components/engine/engine.c`:

```c
extern uint32_t last_rf_code;
static uint32_t last_processed_rf_code = 0;

void engine_check_rf_codes(void) {
    if (last_rf_code == 0 || last_rf_code == last_processed_rf_code) {
        return;  // No new code received
    }
    
    // Check if RF code matches any user
    for (int i = 0; i < user_count; i++) {
        char user_rf_code[16];
        uint32_t user_code = (uint32_t)strtoul(users[i].rf_code, NULL, 16);
        
        if (user_code == last_rf_code) {
            ESP_LOGI("ENGINE", "RF match: %s (%06lX)", users[i].name, last_rf_code);
            
            // Toggle arm state
            if (engine_get_arm_state_safe() == ARM_STATE_DISARMED) {
                engine_arm_away(&users[i]);
            } else {
                engine_disarm_immediate(&users[i]);
            }
            
            last_processed_rf_code = last_rf_code;
            break;
        }
    }
}
```

Call `engine_check_rf_codes()` from `monitor_task()` every 100ms.

## Testing and Debugging

### Test 1: RF Reception

**Goal:** Verify receiver hardware and RMT configuration

```bash
idf.py monitor
```

Trigger RF sensor. Expected output:
```
I (12345) RF: Received RF Code: 0xABCD12
```

**Troubleshooting:**
- No output → Check receiver power (5V), DATA pin connection
- Invalid codes → Adjust `RF_TOLERANCE_US` (try 250-300)
- Intermittent → Improve antenna (17.3cm wire), check for interference

### Test 2: Code Injection (Simulation)

**Goal:** Test RF code processing without hardware

```bash
curl -X POST http://192.168.1.x/api/rf/inject \
  -H "Content-Type: application/json" \
  -d '{"code":"ABCD12"}'
```

Expected response:
```json
{"status":"ok"}
```

Serial output:
```
I (12345) MAIN: RF Injected: ABCD12
```

Check status:
```bash
curl http://192.168.1.x/status | jq .rf
```

Output:
```json
{
  "last": "ABCD12"
}
```

### Test 3: User RF Association

**Goal:** Verify RF code links to user

1. Learn sensor code: `0x123456`
2. Assign to user:
```bash
curl -X POST http://192.168.1.x/api/users/add \
  -d '{"name":"Front Door Sensor","rf":"123456","role":"user"}'
```

3. Trigger sensor
4. Check logs for user match (requires engine integration)

### Test 4: Range and Reliability

**Procedure:**
1. Place receiver at alarm panel location
2. Walk to each sensor location
3. Trigger sensor 10 times
4. Log success rate

**Acceptance Criteria:**
- >95% detection rate at <10m (line of sight)
- >90% detection rate at <15m (through walls)
- <5% false positives (spurious codes)

### Troubleshooting Guide

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| No RF codes detected | Receiver not powered | Check 5V supply, GND connection |
| | DATA pin disconnected | Verify GPIO 4 wiring |
| | Wrong GPIO configured | Check `RF_RECEIVER_GPIO` in rf_driver.c |
| | RMT init failed | Check idf.py build logs |
| Wrong codes received | Timing mismatch | Adjust `RF_BIT_SHORT_US`, `RF_BIT_LONG_US` |
| | Different protocol | Identify sensor chip (PT2262/SC2260/etc) |
| | Tolerance too tight | Increase `RF_TOLERANCE_US` to 300 |
| Intermittent detection | Weak signal | Extend antenna to 17.3cm, vertical orientation |
| | Interference | Move away from WiFi router, power supplies |
| | Low battery | Replace sensor battery |
| Same code from multiple sensors | Sensors not learned | Each sensor should have unique 24-bit code |
| Codes change randomly | Rolling code | Not supported (use fixed-code sensors) |
| RMT task crash | Stack overflow | Increase `xTaskCreate` stack size (3072 → 4096) |

## Advanced Configuration

### Adjusting Sensitivity

**Too many false positives:**
```c
#define RF_TOLERANCE_US          150   // Tighter timing (default: 200)
```

**Missing valid codes:**
```c
#define RF_TOLERANCE_US          300   // Looser timing
```

### Changing GPIO Pin

Edit `rf_driver.c`:
```c
#define RF_RECEIVER_GPIO         40    // Move to GPIO 40
```

Rebuild:
```bash
idf.py build flash
```

### Multiple RF Receivers

For dual-band (433MHz + 315MHz) or redundancy:

```c
// rf_driver.c
#define RF_RECEIVER_433_GPIO     4
#define RF_RECEIVER_315_GPIO     40

void rf_driver_init(void) {
    // Initialize two RMT channels
    rmt_new_rx_channel(&rx_chan_config_433, &rx_channel_433);
    rmt_new_rx_channel(&rx_chan_config_315, &rx_channel_315);
    // ...
}
```

### Custom Protocol Decoder

For proprietary protocols:

```c
// In rf_parser_task()
if (custom_protocol_detect(rx_data.received_symbols)) {
    uint32_t code = custom_protocol_decode(rx_data.received_symbols);
    last_rf_code = code;
    ESP_LOGI(TAG, "Custom RF Code: 0x%06lX", code);
}
```

## Integration with Alarm Engine

### Zone Assignment (Future Feature)

Add RF code field to `zone_t` structure:

```c
typedef struct {
    int gpio;
    char name[32];
    zone_type_t type;
    uint32_t rf_code;  // NEW: RF code for wireless sensor
    // ...
} zone_t;
```

In `zones.json`:
```json
{
  "zones": [
    {
      "gpio": -1,
      "name": "Wireless Front Door",
      "type": "door",
      "rf": "ABCD12",
      "call": "police"
    }
  ]
}
```

### Event Processing

```c
void process_rf_event(uint32_t code) {
    for (int i = 0; i < zone_count; i++) {
        if (zones[i].rf_code == code) {
            ESP_LOGI("ENGINE", "RF Zone triggered: %s", zones[i].name);
            
            // Treat as zone violation
            if (engine_get_arm_state_safe() != ARM_STATE_DISARMED) {
                process_alarm_event(i);
                dispatcher_alert(zones[i].call);
            }
            return;
        }
    }
    
    ESP_LOGW("ENGINE", "Unknown RF code: %06lX", code);
}
```

### Tamper Detection

Detect sensor low battery or removal:

```c
typedef struct {
    uint32_t code;
    uint32_t last_seen_ms;
    bool battery_low;
} rf_sensor_t;

void monitor_rf_sensors(void) {
    uint32_t now = esp_timer_get_time() / 1000;
    
    for (int i = 0; i < sensor_count; i++) {
        if (now - sensors[i].last_seen_ms > 86400000) {  // 24 hours
            ESP_LOGW("RF", "Sensor %06lX not seen in 24h", sensors[i].code);
            // Alert: sensor battery low or removed
        }
    }
}
```

## Performance Characteristics

### Timing

| Parameter | Value | Notes |
|-----------|-------|-------|
| RMT Resolution | 1 MHz (1μs) | `RMT_RESOLUTION_HZ` |
| Symbol buffer | 64 symbols | 128 bytes RAM |
| Parser task | 3072 bytes | Stack size |
| Decode latency | <10ms | RMT ISR → logged code |
| Queue size | 1 event | Single buffered |

### Memory Usage

```
Code (Flash):     ~4KB (rf_driver.c compiled)
Data (RAM):       ~128 bytes (RMT symbols)
Stack (Task):     3072 bytes (rf_parser_task)
Queue:            24 bytes (FreeRTOS queue)
────────────────────────────────────────
Total:            ~3.2KB RAM, 4KB Flash
```

### CPU Load

- **Idle:** 0% (RMT hardware decodes signals)
- **During RF burst:** <2% (parser task processes symbols)
- **Task priority:** 10 (same as monitor_task)

### Range and Reliability

| Environment | Range | Packet Loss |
|-------------|-------|-------------|
| Line of sight | 30-50m | <1% |
| Through 1 wall | 15-20m | <5% |
| Through 2 walls | 8-12m | <10% |
| Metal obstruction | 5m | <20% |

*Values vary by receiver quality and antenna*

## Security Considerations

### Replay Attacks

**Vulnerability:** RF codes can be captured and replayed.

**Mitigations:**
1. **Rate limiting:** Ignore same code within 5 seconds
2. **Time-based codes:** Rotate codes every 24 hours (requires RTC)
3. **Challenge-response:** Two-way communication (requires TX module)

**Implementation:**
```c
static uint32_t last_rf_code_time = 0;

void engine_check_rf_codes(void) {
    uint32_t now = esp_timer_get_time() / 1000;
    
    if (last_rf_code == last_processed_rf_code && 
        now - last_rf_code_time < 5000) {
        return;  // Ignore replay within 5 seconds
    }
    
    // Process code...
    last_rf_code_time = now;
}
```

### Jamming Resistance

**Vulnerability:** 433MHz jammer can block all RF sensors.

**Detection:**
```c
// Monitor RF activity
uint32_t rf_silence_start = 0;

void detect_rf_jamming(void) {
    uint32_t now = esp_timer_get_time() / 1000;
    
    if (last_rf_code != 0) {
        rf_silence_start = 0;  // Activity detected
    } else if (rf_silence_start == 0) {
        rf_silence_start = now;
    } else if (now - rf_silence_start > 60000) {  // 60s silence
        ESP_LOGW("RF", "Possible RF jamming detected");
        // Alert user via wired network
    }
}
```

### False Positives

**Issue:** Neighbor's RF devices may trigger system.

**Solution:** Learn unique codes per sensor (24-bit = 16M combinations).

## API Reference

### C API

```c
/**
 * @brief Initialize RF receiver driver
 * 
 * Configures RMT peripheral on GPIO 4 for 433MHz receiver.
 * Creates parser task to decode PT2262/EV1527 protocol.
 */
void rf_driver_init(void);

/**
 * @brief Last received RF code (global)
 * 
 * Updated by parser task when valid code decoded.
 * 24-bit value (0x000000 - 0xFFFFFF).
 */
extern uint32_t last_rf_code;
```

### REST API

**GET /status**
```json
{
  "rf": {
    "last": "ABCD12"
  }
}
```

**POST /api/rf/inject**

Inject RF code for testing:
```bash
curl -X POST http://IP/api/rf/inject \
  -H "Content-Type: application/json" \
  -d '{"code":"ABCD12"}'
```

Response:
```json
{"status":"ok"}
```

## Recommended Sensors

### Door/Window Sensors

- **Kerui D026** - 433MHz, 2-year battery, 10m range
- **Sonoff DW2** - 433MHz, 3-year battery, magnet switch
- **Aqara Door Sensor** - 433MHz version only (not Zigbee)

### Motion Detectors

- **Kerui P829** - PIR, 433MHz, 12m detection range
- **Sonoff PIR3** - Adjustable sensitivity, low battery alert

### Key Fobs

- **Kerui RC531** - 4 buttons, 50m range, waterproof
- **Broadlink RM4 Pro** - Multi-function, learning capable

### Panic Buttons

- **Kerui Wireless SOS** - Wearable, 100m range
- **Grestek WT-007** - Waterproof, pendant style

## Troubleshooting Commands

```bash
# Check RMT peripheral status
idf.py monitor | grep RF

# Test RF injection
curl -X POST http://IP/api/rf/inject -d '{"code":"123456"}'

# Monitor status endpoint
watch -n 1 "curl -s http://IP/status | jq .rf"

# Enable debug logging
# In sdkconfig: CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y

# Check GPIO configuration
idf.py menuconfig
# Component config → Driver config → RMT Configuration
```

## Future Enhancements

- [ ] **Rolling code support** (Keeloq, HCS301)
- [ ] **RF transmission** (arm/disarm confirmation beeps)
- [ ] **RSSI monitoring** (signal strength tracking)
- [ ] **Multi-protocol decoder** (auto-detect PT2262/SC2260/HT6P20B)
- [ ] **RF sensor battery monitoring** (low battery alerts)
- [ ] **Zone-specific RF codes** (per-zone wireless sensors)
- [ ] **RF repeater mode** (extend range via mesh network)
- [ ] **Encrypted RF** (AES-128 for security-critical sensors)

## References

- **ESP32-S3 TRM:** Chapter 18 - RMT (Remote Control)
- **PT2262 Datasheet:** https://www.princeton.com.tw/
- **EV1527 Datasheet:** http://www.ev-components.com/
- **RF Protocol Database:** https://github.com/sui77/rc-switch/wiki/List_KnownDevices

## Related Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - System design overview
- [CONFIG.md](CONFIG.md) - Configuration file formats
- [DISPLAY_USAGE.md](DISPLAY_USAGE.md) - OLED display integration
- [README.md](README.md) - General setup guide
