# Sentinel-ESP Architecture Guide

## System Overview

**Sentinel-ESP** is a professional-grade alarm system firmware for ESP32-S3 with KC868-A8V3 expansion board. It combines real-time zone monitoring with multi-channel alerting (Email, Telegram, Noonlight professional monitoring) and web-based configuration.

### Core Components
```
┌─────────────────────────────────────────────────────────────┐
│                     Sentinel-ESP Firmware                   │
├──────────────────┬────────────────────┬────────────────────┤
│  Zone Monitoring │   State Machine    │  Alert Dispatch    │
│  (GPIO/I2C)      │   (Arm/Disarm)     │  (Email/SMS/API)   │
├──────────────────┴────────────────────┴────────────────────┤
│  Scheduler (Cron/Intervals) | Device Integrations (ESPHome) │
├────────────────────────────────────────────────────────────┤
│              FreeRTOS Real-Time Kernel (4 Tasks)            │
├────────────────────────────────────────────────────────────┤
│  Storage (SPIFFS/NVS) | Networking (W5500) | Hardware HAL   │
└────────────────────────────────────────────────────────────┘
```

## Multi-Task Architecture

### Task 1: monitor_task (Priority: 10, Very High)
**Purpose**: Real-time zone monitoring with 50ms polling interval

```c
while (true) {
    monitor_scan_all();        // Poll GPIO for all zones
    vTaskDelay(50 / portTICK_PERIOD_MS);  // 50ms cycle
}
```

**Flow**:
1. Read GPIO state for each zone
2. Apply debounce filter (3 consecutive identical readings required)
3. If state changes confirmed: `monitor_process_event(zone_index)`
4. Process event checks `engine_get_arm_state_safe()` and triggers alarm if armed

**Debounce Strategy**:
- Each zone maintains `zone_debounce_counter[zone]`
- Counter increments when GPIO matches expected "secure" state (1)
- Counter resets to 0 when GPIO differs
- Zone considered "changed" when counter reaches 3
- **Purpose**: Prevents false alarms from electrical noise (>150ms signal noise filtered)

### Task 2: mongoose_task (Priority: 5, Normal)
**Purpose**: HTTP web server and MQTT connection management

```c
while (true) {
    mg_mgr_poll(&mgr, 50);     // Service HTTP connections
    if (time_for_mqtt()) {
        start_sentinel_mqtt(); // MQTT heartbeat/sync
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
}
```

**Responsibilities**:
- HTTP server on port 80 (served by Mongoose)
- REST API endpoints:
  - `GET /status` → System state (armed/disarmed/alarmed)
  - `POST /arm` → Arm with delay
  - `POST /disarm` → Disarm with PIN
  - `GET /zones` → Zone list with current states
  - `POST /config` → Update configuration
- MQTT connection with exponential backoff retry
- 30-second heartbeat publish to `sentinel/status/heartbeat`

### Task 3: network_loop_task (Priority: 2, Low)
**Purpose**: Background network and Ethernet event handling

```c
while (true) {
    // Called implicitly by Ethernet event handler
    // Processes IP_EVENT_ETH_GOT_IP and IP_EVENT_ETH_LOST_IP
}
```

**Responsibilities**:
- W5500 Ethernet driver event loop
- DHCP/IP configuration
- Network status tracking (global `g_network_up` flag)
- Recovers from network loss with automatic reconnection

### Task 4: network_watchdog_task (Priority: 5, Normal)
**Purpose**: Tiered network health monitoring and dynamic alert relays

```c
while (s_running) {
    // Wait interval (default 15 mins)
    // Check local gateway (DNS port 53 / Web UI port 80)
    // Check primary external (1.1.1.1 port 53)
    // Check backup external (8.8.8.8 port 53)
    // Update global state and trigger network relays on failure
}
```

**Responsibilities**:
- Detects partial outages (local-only mode) vs total offline states
- Safely probes external internet without relying on specific cloud APIs
- Triggers all relays configured as `network` or `trouble` when the internet is unreachable
- Operates on a dedicated non-blocking timer to avoid stalling main processing
- Updates diagnostic JSON payload (`system_monitoring`) for UI visibility

---

## State Machine: Core Alarm Logic

### Five System States
```
DISARMED (0)
    ↓ [user: arm_system()]
EXITING (1) ← Entry delay countdown (e.g., 30 seconds)
    ↓ [timer expires]
ARMED (2) ← Normal monitoring state (all zones active)
    ↑ [zone trigger & armed]
ENTRY (3) ← Entry delay countdown (e.g., 20 seconds)
    ↓ [timer expires OR zone trigger]
ALARMED (4) ← Full alarm state (sirens blaring, alerts sent)
    ↓ [user: disarm_system() + correct PIN]
DISARMED (0)
```

### State Transition Logic

**Disarmed → Exiting**:
```c
engine_ui_arm(AWAY);  // or STAY, NIGHT
// Sets exit_timer based on arm_delay from config (default 30s)
// Counts down in deciseconds (every 10ms = 1 decisecond)
```

**Exiting → Armed**:
- Timer reaches 0
- All zones now actively monitored
- Any zone violation → triggers alarm

**Armed → Entry**:
```c
// A monitored zone transitions from secure (1) to violated (0)
monitor_process_event(zone_index);
if (is_entry_zone(zone)) {
    engine_trigger_alarm();  // Starts entry timer
    s_arm_state = ENTRY;
    s_timer = entry_delay_cs;  // e.g., 20 seconds
}
```

**Entry → Alarmed**:
- Entry timer expires without disarm PIN entry
- OR another zone triggers during entry period
- Alarm relay activates (siren at max volume)
- All alert channels fire (Telegram, Email, Noonlight)

**Alarmed → Disarmed**:
```c
// User must enter correct PIN via keypad
engine_ui_disarm(pin);
// System validates PIN, cancels alerts, powers down siren
```

### ARM Modes

Three arm modes with different zone activation:

| Mode | All Zones | Perimeter Only | Interior | Use Case |
|------|-----------|---|---|---|
| **AWAY** | ✓ | - | - | Entire house monitored |
| **STAY** | ✗ | ✓ | ✗ | Home occupied, interior disabled |
| **NIGHT** | ✗ | ✓ | Select | Bedroom monitored, living areas off |

---

## Zone & Relay Management

### Zone Types (Type-Safe Enums)
```c
typedef enum {
    ZONE_TYPE_DOOR,           // Contact sensor on doors
    ZONE_TYPE_WINDOW,         // Contact sensor on windows
    ZONE_TYPE_MOTION,         // PIR motion detector
    ZONE_TYPE_GLASS_BREAK,    // Acoustic glass break sensor
    ZONE_TYPE_FIRE,           // Smoke detector
    ZONE_TYPE_SMOKE,          // Additional smoke monitoring
    ZONE_TYPE_PANIC,          // Panic button (24/7 active)
    ZONE_TYPE_MEDICAL,        // Medical alert button
    ZONE_TYPE_TEMP,           // Temperature sensor
    ZONE_TYPE_WATER,          // Water leak detector
} zone_type_t;
```

**Conversion Functions**:
```c
// String input (from JSON config) → Enum
zone_type_t type = zone_type_from_string("fire");

// Enum → String output (for logging/display)
const char *name = zone_type_to_string(ZONE_TYPE_FIRE);  // "fire"
```

### Relay Types (Output Control)
```c
typedef enum {
    RELAY_TYPE_ALARM,         // Primary alarm siren/bell
    RELAY_TYPE_SIREN,         // Secondary siren
    RELAY_TYPE_STROBE,        // Visual alarm (strobe light)
    RELAY_TYPE_CHIME,         // Entry chime
    RELAY_TYPE_BELL,          // Phone line alert
    RELAY_TYPE_LIGHT,         // Outdoor light (deter intruders)
    RELAY_TYPE_DOOR_LOCK,     // Electronic door lock
    RELAY_TYPE_GATE,          // Gate/barrier control
} relay_type_t;
```

### Hardware Relay Mapping (KC868-A8V3)
```c
const int relay_pins[8] = {2, 4, 5, 13, 15, 18, 19, 21};

// Example configuration:
// Relay 0 → Pin 2  → Alarm siren (primary)
// Relay 1 → Pin 4  → Strobe light
// Relay 2 → Pin 5  → Front door lock
// Relay 3 → Pin 13 → Chime
// Relay 4-7 → Available for expansion
```

---

## Real-Time Clock (DS3231)

### Battery-Backed Timekeeping

The DS3231 provides accurate timekeeping with battery backup, ensuring system time persists across power cycles.

**Key Features**:
- Temperature-compensated crystal oscillator (TCXO) for ±2ppm accuracy
- CR2032 battery backup (5+ years)
- Integrated temperature sensor (±3°C accuracy)
- Automatic oscillator stop detection (OSF flag)
- BCD-encoded time registers

### Boot-Time Time Restoration

```c
// On system boot (main.c)
system_monitoring_init(bus_handle);
    ↓
system_monitoring_check_rtc_status()
    ├─ Read status register 0x0F
    ├─ Check OSF (Oscillator Stop Flag)
    └─ Clear OSF if set (indicates power loss)
    ↓
system_monitoring_get_rtc_time()
    ├─ Read time registers 0x00-0x06 (BCD encoded)
    ├─ Convert BCD → decimal
    └─ Return Unix timestamp
    ↓
if (rtc_time > 2021-01-01) {
    settimeofday(&tv, NULL);  // Restore system time
    display_time_on_oled();   // Show "RTC: MM/DD HH:MM"
}
```

**Fallback Behavior**:
- RTC valid → Use RTC time immediately
- RTC invalid/stopped → Use epoch time, wait for NTP
- Network available → Sync NTP → Update RTC
- Network unavailable → Use RTC only

### NTP Synchronization

```c
system_monitoring_sync_rtc("pool.ntp.org")
    ↓
esp_sntp_init()  // Start SNTP client
    ↓
Wait up to 10 seconds for sync
    ↓
if (sync_success) {
    system_monitoring_set_rtc_time(time(NULL));
    // Write BCD-encoded time to DS3231 registers 0x00-0x06
    g_current_status.rtc_synchronized = true;
}
```

### BCD Encoding (DS3231 Requirement)

The DS3231 stores time in Binary-Coded Decimal format:

```c
#define DEC2BCD(val) (((val / 10) << 4) | (val % 10))
#define BCD2DEC(val) (((val >> 4) * 10) + (val & 0x0F))

// Example: 45 seconds → 0x45 BCD
// 0x45 = 0100 0101 = (4 × 10) + 5 = 45

// Write time to RTC
uint8_t write_buf[8] = {
    0x00,                           // Register address
    DEC2BCD(tm_info->tm_sec),       // 0x00: Seconds (00-59)
    DEC2BCD(tm_info->tm_min),       // 0x01: Minutes (00-59)
    DEC2BCD(tm_info->tm_hour),      // 0x02: Hours 24h (00-23)
    DEC2BCD(tm_info->tm_wday + 1),  // 0x03: Day of week (1-7)
    DEC2BCD(tm_info->tm_mday),      // 0x04: Date (01-31)
    DEC2BCD(tm_info->tm_mon + 1),   // 0x05: Month (01-12)
    DEC2BCD(tm_info->tm_year % 100),// 0x06: Year (00-99)
};
```

### Temperature Monitoring

```c
int8_t temp_c;
uint16_t temp_raw;
system_monitoring_read_temperature(&temp_c, &temp_raw);

// DS3231 registers 0x11 (MSB) and 0x12 (LSB)
// Resolution: 0.25°C (10-bit)
// Range: -40°C to +85°C
// Used for: System health monitoring, OLED display
```

### TOTP Time Dependency

The RTC is **critical** for RFC 6238 TOTP 2FA:

```c
// TOTP code generation (otp.c)
time_t now = time(NULL);  // Must be accurate!
uint64_t time_step = now / 30;  // 30-second window
generate_totp_hmac_sha1(secret, time_step, &code);

// RTC ensures:
// ✓ Correct TOTP codes even without network
// ✓ Time persists across power cycles
// ✓ Authentication works offline
```

**Without RTC**: System time resets to epoch (1970-01-01) on boot → TOTP codes invalid → Authentication fails until NTP sync.

**With RTC**: System time restored immediately → TOTP codes valid → Authentication works instantly.

### Oscillator Stop Detection

```c
// Check if RTC lost power (battery dead/removed)
int status = system_monitoring_check_rtc_status();

if (status == ESP_FAIL) {
    ESP_LOGW(TAG, "RTC oscillator was stopped - time may be incorrect");
    // OSF bit was set in register 0x0F
    // Indicates: Battery failure, power loss, or first boot
    // Action: Force NTP sync, warn user
}
```

### API Reference

```c
// Initialize system monitoring and RTC
int system_monitoring_init(void* i2c_bus_handle);

// Check RTC oscillator status (OSF flag)
int system_monitoring_check_rtc_status(void);

// Read battery-backed time from DS3231
uint32_t system_monitoring_get_rtc_time(void);

// Write time to DS3231 (BCD encoded)
int system_monitoring_set_rtc_time(uint32_t timestamp);

// Sync RTC with NTP server
int system_monitoring_sync_rtc(const char *ntp_server);

// Read DS3231 temperature sensor
int system_monitoring_read_temperature(int8_t *temp_c, uint16_t *temp_raw);
```

---

## Configuration System

### Storage Hierarchy
```
┌─────────────────────────────────┐
│     SPIFFS Filesystem           │
│  (/data/ mount point)           │
├──────────────┬──────────────────┤
│ zones.json   │ Zone definitions │
│ relays.json  │ Relay mappings   │
│ users.json   │ User accounts    │
│ config.json  │ System settings  │
│ network.json │ Network config   │
├─────────────────────────────────┤
│     NVS (Non-Volatile Storage)  │
│  (key-value namespace)          │
├──────────────┬──────────────────┤
│ arm_state    │ Last arm state   │
│ last_alarm   │ Last alarm info  │
│ auth_fails   │ Rate limiting    │
└─────────────────────────────────┘
```

### NVS Persistence (State Recovery)

**On State Change**:
```c
void engine_set_arm_state_safe(arm_state_t new_state) {
    // Thread-safe state update
    xSemaphoreTake(engine_state_mutex, ...);
    s_arm_state = new_state;
    xSemaphoreGive(engine_state_mutex);
    
    // Persist to NVS (survives power loss)
    engine_save_state_to_nvs();  // arm_state key
}
```

**On Startup**:
```c
void app_main() {
    // ... initialization ...
    engine_restore_state_from_nvs();  // Recover last state
    
    // If was ALARMED before restart → Alarm again (security feature!)
    if (engine_get_arm_state_safe() == ALARMED) {
        engine_trigger_alarm();  // Siren + alerts continue
    }
}
```

### Configuration Files

#### zones.json
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
    },
    {
      "index": 1,
      "name": "Kitchen Motion",
      "type": "motion",
      "pin": 33,
      "arm_mode": "stay",
      "is_alert_zone": true
    }
  ]
}
```

#### users.json
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
    }
  ]
}
```

#### config.json
```json
{
  "system": {
    "location": "Home",
    "arm_delay_seconds": 30,
    "entry_delay_seconds": 20,
    "siren_duration_seconds": 300,
    "log_level": 2
  },
  "mqtt": {
    "broker": "mqtt.example.com",
    "port": 8883,
    "username": "sentinel",
    "password": "***"
  },
  "noonlight": {
    "enabled": true,
    "api_key": "***",
    "account_id": "acc_12345"
  },
  "telegram": {
    "enabled": true,
    "bot_token": "***",
    "chat_id": "1234567890"
  }
}
```

---

## Alert Dispatch System (Multi-Channel)

### Alert Routing Decision Tree
```
Zone triggers alarm
    ↓
dispatcher_alert(zone, alert_type)
    ├─ Check if alert type enabled in config
    │  ├─ notify=1 → No alerts
    │  ├─ notify=2 → Email only
    │  ├─ notify=3 → Telegram only
    │  ├─ notify=4 → Noonlight professional
    │  └─ notify=5 → Multiple channels
    │
    ├─ Prevent duplicate alerts (is_alert_sent flag)
    │
    ├─ Route based on type:
    │  ├─ FIRE → dispatcher_alert(FIRE)
    │  ├─ POLICE → dispatcher_alert(POLICE)
    │  ├─ MEDICAL → dispatcher_alert(MEDICAL)
    │  └─ OTHER → dispatcher_alert(OTHER)
    │
    └─ Call service functions:
       ├─ telegram_send_alert(&config, &zone)
       ├─ smtp_alert_all_contacts(&config, users, user_count, &zone)
       ├─ noonlight_create_alarm(&config, &zone, slot)
       └─ [Future: SMS, pushbullet, HomeKit, etc.]
```

### Payload Builder Pattern
```c
// Service-specific JSON construction
payload_build_noonlight(&config, &zone, false);
// payload_buffer now contains:
// {"location": "...", "zone": "...", "type": "fire", ...}

payload_build_telegram(&config, &zone);
// payload_buffer now contains:
// {"chat_id": "...", "text": "🚨 Front Door triggered!", ...}

payload_build_smtp(&config, &zone, &user);
// payload_buffer now contains:
// {"to": "admin@home.com", "subject": "Alarm: Front Door", ...}
```

### HTTP Request Flow
```
dispatcher_alert()
    ↓
telegram_send_alert() → net_req(TELEGRAM_API, payload, bearer, "POST")
    ↓
net_req() [Unified HTTP Engine]
    ├─ ESP32: Uses esp_http_client (non-blocking)
    │  └─ HTTP event handler processes response asynchronously
    └─ Linux: Uses libcurl (for testing)
```

**Non-Blocking Design**: HTTP requests queue immediately and return. Responses handled by event callbacks. Tasks never block on network I/O.

---

## Authentication & Rate Limiting

### PIN Validation Flow
```c
// User enters PIN via keypad (4-8 digits)
engine_check_keypad(pin_string);
    ↓
// Check against master PIN first
if (strcmp(pin, config.master_pin) == 0) {
    engine_ui_disarm(pin);  // Disarm system
    auth_state.failed_attempts = 0;
}
    ↓
// Check against user list
else if (user_exists(pin)) {
    engine_ui_disarm(pin);
    auth_state.failed_attempts = 0;
}
    ↓
// Incorrect PIN
else {
    auth_state.failed_attempts++;
    LOG_WARN("Invalid PIN attempt %d/5", auth_state.failed_attempts);
    
    if (auth_state.failed_attempts >= 5) {
        auth_state.lockout_until = now + 60;  // 60 second lockout
        LOG_ERROR("Authentication lockout activated");
    }
}
```

### Brute Force Protection
```c
// Before checking keypad input
if (auth_state.lockout_until > now) {
    time_remaining = auth_state.lockout_until - now;
    LOG_WARN("Lockout active for %d more seconds", time_remaining);
    return DISARM_FAILED;
}
```

---

## Network & MQTT Connectivity

### Ethernet Initialization (W5500 over SPI2)
```c
void init_ethernet_v3() {
    // SPI2 bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 11,
        .miso_io_num = 13,
        .sclk_io_num = 12,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8000,
    };
    
    // W5500 device configuration
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = 25 * 1000 * 1000,  // 25MHz SPI clock
        .spics_io_num = 10,                  // Chip select GPIO 10
        .queue_size = 20,
    };
    
    // W5500 MAC/PHY configuration
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 0;
    phy_cfg.reset_gpio_num = 5;  // PHY reset
    
    // Interrupts
    esp_eth_intr_gpio_config_t intr_cfg = {
        .intr_gpio_num = 4,  // Interrupt GPIO
    };
    
    // Create Ethernet interface
    esp_netif_t *eth_netif = esp_netif_new(&esp_netif_config_eth);
    esp_eth_handle_t eth_handle = esp_eth_new_mac(&mac_cfg, &phy_cfg);
    esp_eth_driver_install(eth_handle, ...);
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
    esp_eth_start(eth_handle);
}
```

### MQTT Exponential Backoff Retry
```
Connection attempt #1 → Wait 5 seconds
Connection attempt #2 → Wait 15 seconds (5 × 3)
Connection attempt #3 → Wait 30 seconds (15 × 2)
Connection attempt #4 → Wait 60 seconds (30 × 2)
Connection attempt #5+ → Wait 60 seconds (max)
```

**Code Pattern**:
```c
void start_sentinel_mqtt() {
    static int retry_count = 0;
    static uint32_t last_attempt = 0;
    
    const uint32_t retry_delays[] = {5000, 15000, 30000, 60000};
    uint32_t delay_ms = retry_delays[MIN(retry_count, 3)];
    
    if (now - last_attempt < delay_ms) {
        return;  // Not yet time to retry
    }
    
    if (mqtt_connect(...)) {
        retry_count = 0;  // Reset on success
    } else {
        retry_count++;
        last_attempt = now;
    }
}
```

### MQTT Topics
```
// Status publishing (30s heartbeat)
sentinel/status/heartbeat
  → {"arm_state": 2, "zones": [...], "battery": 92}

// System state changes
sentinel/state/arm_state
  → 0 (DISARMED) / 1 (EXITING) / 2 (ARMED) / 3 (ENTRY) / 4 (ALARMED)

// Remote commands (subscribe)
sentinel/alarm/set
  → {"action": "arm", "mode": "away", "delay": 30}
  → {"action": "disarm", "pin": "1234"}
```

---

## Logging System

### Five Log Levels
```c
typedef enum {
    LOG_LEVEL_DEBUG = 0,   // Verbose development output
    LOG_LEVEL_INFO = 1,    // Normal operation messages
    LOG_LEVEL_WARN = 2,    // Warning conditions (default)
    LOG_LEVEL_ERROR = 3,   // Error conditions
    LOG_LEVEL_CRITICAL = 4 // System critical failures
} log_level_t;
```

### Usage Examples
```c
LOG_DEBUG("Zone state change detected at GPIO %d", gpio_num);
LOG_INFO("System armed in AWAY mode with 30s exit delay");
LOG_WARN("MQTT connection failed, retrying with 15s backoff");
LOG_ERROR("NVS write failed: %s", esp_err_to_name(err));

// Runtime control
logging_set_level(LOG_LEVEL_DEBUG);  // Development: verbose
logging_set_level(LOG_LEVEL_WARN);   // Production: normal
```

### Platform-Specific Implementation
- **ESP32**: Native ESP-IDF logging (ESP_LOGD/LOGI/LOGW/LOGE)
- **Linux**: ANSI colored console output for testing

---

## Critical Sections & Thread Safety

### Mutex-Protected State
```c
static SemaphoreHandle_t engine_state_mutex = NULL;

// Thread-safe getters
arm_state_t engine_get_arm_state_safe() {
    xSemaphoreTake(engine_state_mutex, portMAX_DELAY);
    arm_state_t state = s_arm_state;
    xSemaphoreGive(engine_state_mutex);
    return state;
}

// Thread-safe setters with NVS persistence
void engine_set_arm_state_safe(arm_state_t new_state) {
    xSemaphoreTake(engine_state_mutex, portMAX_DELAY);
    s_arm_state = new_state;
    engine_save_state_to_nvs();
    xSemaphoreGive(engine_state_mutex);
}
```

### Race Condition Prevention

**Problem**: monitor_task (priority 10) and mongoose_task (priority 5) access `s_arm_state`

**Solution**: All state access protected by `engine_state_mutex`
- monitor_task acquires mutex before checking if system is armed
- mongoose_task acquires mutex before state changes
- NVS write inside critical section ensures consistency

---

## Recovery & Fault Tolerance

### Power Loss Recovery
```
Device loses power during ARMED state
    ↓
Device restarts
    ↓
engine_restore_state_from_nvs() loads previous state
    ↓
If state was ALARMED → Siren activates immediately
If state was ARMED → Resume monitoring
If state was EXITING → Resume exit delay countdown
```

### Network Loss Recovery
```
Network connection lost (IP_EVENT_ETH_LOST_IP)
    ↓
g_network_up = false  // Global flag
    ↓
MQTT disconnect (queued for reconnect)
    ↓
Monitoring continues locally (zones still active)
    ↓
Network restored → IP_EVENT_ETH_GOT_IP
    ↓
MQTT reconnect with exponential backoff
    ↓
Resync state to cloud/broker
```

### Watchdog Timer
```c
// Prevent firmware hang/crash
esp_task_wdt_add(xTaskGetCurrentTaskHandle());

// Pet the watchdog periodically (< 10 second timeout)
in monitor_task:    every 50ms ✓
in mongoose_task:   every 50ms ✓
```

---

## Performance Metrics

| Metric | Target | Measured |
|--------|--------|----------|
| Zone poll latency | <50ms | ~5ms (32 zones) |
| Debounce filter delay | ~150ms | 150ms (3 × 50ms) |
| ARM/DISARM response | <100ms | ~20ms (keypad to state change) |
| Alert dispatch | <1s | ~200ms (HTTP queue + send) |
| HTTP web response | <500ms | ~100ms (Mongoose poll cycle) |
| MQTT publish interval | 30s | 30s ± 5s |
| GPIO bounce immunity | >150ms | Yes (debounce filter) |
| Memory: DRAM | <150KB | ~120KB (3 tasks + buffers) |
| Memory: SPIFFS | <500KB | ~200KB (configs + logs) |

---

## Deployment Checklist

- [ ] All zones configured in zones.json with correct GPIO pins
- [ ] All relays mapped in relays.json with correct outputs
- [ ] Master PIN set in config.json (different from 1234)
- [ ] Email server configured (SMTP credentials)
- [ ] Telegram bot token and chat ID set
- [ ] Noonlight API credentials configured (if using)
- [ ] MQTT broker address and credentials set
- [ ] Network configuration (DHCP or static IP)
- [ ] Time zone and NTP server configured
- [ ] **RTC battery installed (CR2032) and functional**
- [ ] **RTC time synced with NTP on first boot**
- [ ] **RTC OSF flag cleared (no oscillator stop warning)**
- [ ] **Boot-time RTC restoration verified (check OLED display)**
- [ ] Watchdog timer enabled
- [ ] Log level set to WARN for production
- [ ] NVS factory reset (if migrating from old version)
- [ ] Device rebooted and state recovered verified
- [ ] Zone test: each zone triggers alarm when armed
- [ ] Alert test: email/Telegram/Noonlight delivered
- [ ] Web UI accessible on local network
- [ ] MQTT heartbeat visible on broker

---

## Development Guide

### Adding a New Zone Type
1. Add enum value to `zone_type_t` in storage_mgr.h
2. Update `zone_type_from_string()` with case
3. Update `zone_type_to_string()` with case
4. Update config schema documentation
5. Test with `zone_type_from_string("name")`

### Adding a New Alert Channel
1. Create new function `protocol_send_alert(config, zone)`
2. Add to `dispatcher_alert()` switch statement
3. Create `payload_build_protocol()` if needed
4. Test with mock HTTP client on Linux
5. Update `config_t` structure with credentials

### Debugging Zone Events
```c
// Enable verbose logging
logging_set_level(LOG_LEVEL_DEBUG);

// Add custom logging in monitor.c:
LOG_DEBUG("Zone %d: GPIO %d → %d (debounce: %d)", 
    index, zone_pins[index], new_state, zone_debounce_counter[index]);

// Watch monitor_task output
idf.py monitor  /dev/ttyUSB0
```

---

## Related Documentation

- **[CONFIG.md](CONFIG.md)** - Configuration file reference
- **[API.md](API.md)** - REST API documentation
- **[MQTT.md](MQTT.md)** - MQTT topic reference
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Common issues and fixes

