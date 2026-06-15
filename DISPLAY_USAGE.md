# OLED Display Usage - SSD1306 128x64

## Overview

The Sentinel alarm system uses a 128x64 pixel SSD1306 OLED display (0.96" or 1.3") connected via I2C to provide real-time system status feedback. The display is thread-safe and protected by a mutex for concurrent access.

## Hardware Configuration

**Display Specifications:**
- Model: SSD1306 128x64 OLED
- Interface: I2C (I2C_NUM_0)
- I2C Address: 0x3C
- Clock Speed: 400 kHz
- Pins: SDA=GPIO8, SCL=GPIO18

**Shared I2C Bus:**
The OLED shares I2C bus with:
- DS3231 RTC (0x68)
- 24C02 EEPROM (0x50)
- PCF8575 IO Expander (0x22)

## Display Layout

The display uses 4 text rows (0-3) for different categories of information:

```
┌─────────────────────────┐
│ Row 0: SYSTEM STATE     │  Primary status (ARM/DISARM/ALARM)
│ Row 1: EVENT DETAILS    │  Zone names, user names, alerts
│ Row 2: NETWORK INFO     │  IP address, connectivity status
│ Row 3: SYSTEM HEALTH    │  Temperature, tamper, UPS status
└─────────────────────────┘
```

### Row Assignments

| Row | Purpose | Update Frequency | Examples |
|-----|---------|------------------|----------|
| 0 | System State | On state change | `DISARMED`, `ARMED AWAY`, `*** ALARM ***` |
| 1 | Event Details | On trigger/action | `ALARM! Front Door`, `Armed by John` |
| 2 | Network Status | On IP change | `192.168.1.100`, `Network DOWN` |
| 3 | System Health | Every 100ms | `Temp: 72°F`, `TAMPER!`, `UPS Active` |

## Display Updates

### Automatic Updates

The display automatically updates on these events:

#### 1. **System State Changes** (Row 0)
Triggered by: ARM/DISARM actions via keypad or web API

**States Displayed:**
- `DISARMED` - State 0
- `ARMED AWAY` - State 1  
- `ARMED STAY` - State 2
- `ARMED NIGHT` - State 3
- `*** ALARM ***` - State 4

**Implementation:**
```c
// In engine.c - engine_set_arm_state_safe()
display_update_status("ARMED AWAY", NULL);
```

#### 2. **Zone Triggers** (Row 0-1)
Triggered by: Zone violations during armed state

**Display Format:**
```
*** ALARM ***
ALARM! Front Door
```

**Implementation:**
```c
// In engine.c - process_alarm_event()
char alarm_msg[32];
snprintf(alarm_msg, sizeof(alarm_msg), "ALARM! %s", zones[index].name);
display_update_status("*** ALARM ***", alarm_msg);
```

#### 3. **Network Connection** (Row 0 + 2)
Triggered by: IP address assignment (DHCP or static)

**Display Format:**
```
ARMED AWAY
192.168.1.100
```

**Implementation:**
```c
// In main.c - got_ip_event_handler()
ssd1306_display_text(oled_dev, 2, ip_str, false);
```

#### 4. **System Health** (Row 3)
Triggered by: Monitor task every 100ms

**Display Format:**
- `Temp: 72°F` - Normal operation
- `TAMPER!` - Case opened (DI7 triggered)
- `UPS Active` - Main power lost (DI8 triggered)

**Implementation:**
```c
// In main.c - monitor_task()
ssd1306_display_text(oled_dev, 3, temp_line, false);
```

#### 5. **Boot Sequence**
Shows initialization progress:
1. `BOOTING...` - Initial startup
2. `SD DETECTED` - If SD card present
3. `Ethernet initialized` - Network ready
4. `Sentinel Online` - System operational

## API Reference

### Display Update Function

```c
void display_update_status(const char* state_text, const char* detail_text);
```

**Purpose:** Update display with new state and optional detail information

**Parameters:**
- `state_text` - Text for row 0 (state), can be NULL
- `detail_text` - Text for row 1 (details), can be NULL

**Thread Safety:** Uses `i2c_mutex` with 100ms timeout

**Example Usage:**
```c
// Show armed state only
display_update_status("ARMED AWAY", NULL);

// Show alarm with zone
display_update_status("*** ALARM ***", "ALARM! Kitchen");

// Clear display (both NULL shows blank)
display_update_status(NULL, NULL);
```

### Direct Display Functions

For advanced usage (requires manual mutex management):

```c
// Must wrap in mutex:
if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ssd1306_clear_display(oled_dev, false);
    ssd1306_display_text(oled_dev, row, "Text", false);
    xSemaphoreGive(i2c_mutex);
}
```

**Available Functions:**
- `ssd1306_clear_display(dev, false)` - Clear entire display
- `ssd1306_display_text(dev, row, text, false)` - Write to row 0-3
- Row parameter: 0-3 (0=top, 3=bottom)
- Final bool: `false` = overwrite, `true` = invert

## Thread Safety

### I2C Mutex Protection

**Global Mutex:** `i2c_mutex` (created in app_main)

**Usage Pattern:**
```c
if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Perform I2C operations
    ssd1306_display_text(oled_dev, 0, "Status", false);
    xSemaphoreGive(i2c_mutex);
} else {
    ESP_LOGW(TAG, "I2C mutex timeout");
}
```

**Important Rules:**
1. ✅ Always acquire mutex before OLED access
2. ✅ Use 100ms timeout for boot/events, 50ms for monitoring
3. ✅ Always release mutex after operations
4. ❌ Never block indefinitely on mutex
5. ❌ Never call display functions from ISR context

### Concurrent Access

The system has multiple concurrent display users:
- **Main task** - Boot sequence, network events
- **Monitor task** - System health (Row 3)
- **Engine** - State changes, alarms (via display_update_status)
- **Event handlers** - Network status, authentication

All properly synchronized via `i2c_mutex`.

## Common Use Cases

### 1. Add Custom State Display

```c
// In your component
extern void display_update_status(const char* state_text, const char* detail_text);

void my_function() {
    display_update_status("MAINTENANCE", "Updating firmware");
    // Your code here
    vTaskDelay(pdMS_TO_TICKS(2000));
    display_update_status("DISARMED", NULL);
}
```

### 2. Show Temporary Message

```c
void show_temp_message(const char* message) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ssd1306_display_text(oled_dev, 1, message, false);
        xSemaphoreGive(i2c_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(3000));  // Show for 3 seconds
    // Clear by restoring normal display
}
```

### 3. Update Network Status

```c
void show_network_status(bool connected) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (connected) {
            ssd1306_display_text(oled_dev, 2, "Network: OK", false);
        } else {
            ssd1306_display_text(oled_dev, 2, "Network DOWN", false);
        }
        xSemaphoreGive(i2c_mutex);
    }
}
```

### 4. Authentication Feedback

```c
void show_auth_result(bool success, const char* username) {
    char msg[32];
    if (success) {
        snprintf(msg, sizeof(msg), "Armed by %s", username);
        display_update_status("ARMED AWAY", msg);
    } else {
        display_update_status("AUTH FAILED", "Invalid code");
        vTaskDelay(pdMS_TO_TICKS(2000));
        display_update_status("DISARMED", NULL);
    }
}
```

## Troubleshooting

### Display Not Updating

**Symptom:** Display shows old or blank content

**Possible Causes:**
1. I2C mutex deadlock - Check for missing `xSemaphoreGive()`
2. I2C bus error - Check wiring, address (0x3C), pullups
3. Display not initialized - Verify `ssd1306_init()` succeeded

**Debug Steps:**
```c
// Enable verbose I2C logging
esp_log_level_set("i2c", ESP_LOG_DEBUG);

// Test display directly
if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ssd1306_clear_display(oled_dev, false);
    ssd1306_display_text(oled_dev, 0, "TEST", false);
    xSemaphoreGive(i2c_mutex);
} else {
    ESP_LOGE(TAG, "Cannot acquire I2C mutex!");
}
```

### Garbled Display

**Symptom:** Random characters or corrupted output

**Possible Causes:**
1. I2C timing issues - Reduce clock speed to 100kHz
2. Electrical noise - Add capacitors, shorter wires
3. Concurrent access without mutex - Review all OLED calls

**Fix:**
```c
// In main.c - reduce I2C speed
ssd1306_config_t oled_conf = {
    .i2c_address = 0x3C,
    .i2c_clock_speed = 100000,  // Try slower speed
    .panel_size = SSD1306_PANEL_128x64,
};
```

### Mutex Timeout Warnings

**Symptom:** Logs show "I2C mutex timeout"

**Causes:**
- Monitor task holding mutex too long
- Display operations taking excessive time
- Deadlock condition

**Solution:**
```c
// Reduce critical section time
if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Prepare data BEFORE taking mutex
    char line[32];
    snprintf(line, sizeof(line), "Temp: %d°F", temp);
    
    // Quick I2C operation only
    ssd1306_display_text(oled_dev, 3, line, false);
    xSemaphoreGive(i2c_mutex);
}
```

## Performance Considerations

### Update Frequency

**Current Rates:**
- State changes: On-demand (immediate)
- System health: 100ms (10 Hz)
- Network events: On-demand
- Boot messages: One-time

**Recommendations:**
- ✅ State/alarm updates: Immediate (good UX)
- ✅ System health: 100-500ms acceptable
- ⚠️ Don't update faster than 50ms (20 Hz max)
- ❌ Never update in tight loops

### I2C Bandwidth

**Display Refresh Time:** ~15ms full screen @ 400kHz

**Bandwidth Usage:**
- Single line update: ~3-5ms
- Full clear + 4 lines: ~15-20ms
- Mutex overhead: <1ms

**Best Practices:**
1. Update only changed rows
2. Batch updates when possible
3. Use `display_update_status()` for common cases
4. Avoid full screen clears if unnecessary

## Future Enhancements

Potential improvements for consideration:

### 1. Display Brightness Control
```c
// Add to hardware_config.h
#define OLED_BRIGHTNESS_DAY    255
#define OLED_BRIGHTNESS_NIGHT  100

void display_set_brightness(uint8_t level);
```

### 2. Scrolling Long Messages
```c
void display_scroll_text(uint8_t row, const char* long_text);
```

### 3. Icons/Graphics
```c
// Show icons for state
void display_draw_icon(uint8_t x, uint8_t y, const uint8_t* icon_data);
```

### 4. Display Timeout/Sleep
```c
// Auto-dim after inactivity
void display_set_timeout(uint32_t seconds);
```

## References

- **SSD1306 Component:** `managed_components/k0i05__esp_ssd1306/`
- **Display Functions:** [main/main.c](main/main.c#L531) - `display_update_status()`
- **State Updates:** [components/engine/engine.c](components/engine/engine.c#L445) - `engine_set_arm_state_safe()`
- **Monitor Task:** [main/main.c](main/main.c#L460) - `monitor_task()`
- **I2C Config:** [components/engine/hardware_config.h](components/engine/hardware_config.h#L4) - Pin definitions

## Version History

- **v1.2** (2026-01-28) - Added automatic state/alarm display updates
- **v1.1** (2026-01-27) - Fixed I2C mutex protection, removed duplicate bus init
- **v1.0** (2026-01-21) - Initial OLED integration with system monitoring
