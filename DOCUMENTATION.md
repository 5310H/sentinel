# Documentation Index

**⚠️ UPDATED INDEX AVAILABLE**  
**Please use [INDEX.md](INDEX.md) for the current complete documentation index.**  
This file contains legacy documentation references.

Complete documentation for Sentinel-ESP alarm system firmware.

## Project Overview Documents

### [README.md](README.md)
Main project documentation with:
- Feature highlights (security, connectivity, reliability)
- Quick start guide (build, flash, web UI)
- Hardware architecture (I2C, SPI, GPIO, RF mappings)
- System architecture overview
- Component listing with line counts
- API endpoints and MQTT topics
- Development and testing guide

**Target Audience**: Project managers, developers, end users  
**Length**: ~300 lines  
**Key Sections**: Features, quick start, hardware, system design

---

## Architecture & Design

### [ARCHITECTURE.md](ARCHITECTURE.md)
Comprehensive system design documentation covering:
- System overview with component diagram
- Multi-task architecture (3 FreeRTOS tasks, priority levels)
- Zone monitoring debounce strategy (150ms noise immunity)
- 5-state alarm machine with timing and transitions
- 3 arm modes (Away, Stay, Night) with zone activation
- Zone/relay management with type-safe enums
- Configuration system (SPIFFS hierarchy, NVS persistence)
- Alert dispatch system (multi-channel routing)
- MQTT with exponential backoff retry (5s→15s→30s→60s)
- Logging system (5 levels, platform-specific)
- Thread safety with mutex protection
- Recovery mechanisms (power loss, network loss)
- Performance metrics and targets
- Deployment checklist (20 items)
- Development guide (adding features)

**Target Audience**: Architects, senior developers, system integrators  
**Length**: ~800 lines  
**Key Sections**: Multi-task design, state machine, alert routing, recovery

---

### [CONFIG.md](CONFIG.md)
Configuration reference covering:
- Quick start (build, flash, web UI access)
- Configuration file specifications:
  - zones.json (32 zones, GPIO mapping, arm modes)
  - relays.json (8 relays, types, control)
  - users.json (up to 16 users, PINs, permissions)
  - config.json (system, MQTT, Telegram, Noonlight, SMTP)
  - network.json (Ethernet, WiFi, mDNS)
- GPIO pin configuration:
  - Relay outputs (fixed mapping to GPIO 2,4,5,13,15,18,19,21)
  - I2C bus (GPIO 8/9)
  - SPI bus for W5500 (GPIO 10-13)
  - Zone inputs (GPIO 0-49, avoiding conflicts)
  - RF receiver (GPIO 3)
- Zone type reference (10 types with use cases)
- Alert routing examples (fire, intruder, ignored scenarios)
- Building and flashing instructions
- Troubleshooting guide (5 common issues)
- Performance tuning options
- Factory reset procedures

**Target Audience**: System integrators, end users, support staff  
**Length**: ~500 lines  
**Key Sections**: Configuration files, GPIO mapping, troubleshooting

---

## Code Documentation

### [IMPROVEMENTS.md](IMPROVEMENTS.md)
Summary of all code improvements with:
- Phase 1: Critical security fixes (5 issues)
  - Race conditions (mutex protection)
  - Buffer overflows (format truncation)
  - GPIO validation (bounds checking)
  - Array bounds (comprehensive checks)
  - Authentication (rate limiting)
- Phase 2: Production improvements (4 features)
  - Non-blocking HTTP (async Telegram)
  - MQTT resilience (exponential backoff)
  - Type-safe enums (zone/relay types)
  - Configurable logging (5 levels)
- Phase 3: Critical infrastructure (4 fixes)
  - Ethernet initialization
  - Watchdog timer
  - Network loss detection
  - NVS persistence
- Phase 4: Documentation (4 deliverables)
  - Function-level Doxygen comments (60+ functions)
  - Architecture guide
  - Configuration reference
  - README update
- Testing recommendations
- Deployment checklist

**Target Audience**: Technical leads, code reviewers, QA  
**Length**: ~400 lines  
**Key Sections**: Security fixes, reliability features, test plan

---

## Function-Level Documentation (Doxygen Comments)

All public functions documented in header files with @brief, @param, @return, and @example tags:

### Core Engine
- **engine.h** (13 functions)
  - engine_init(), engine_tick(), engine_check_keypad()
  - engine_ui_arm(), engine_ui_disarm(), engine_trigger_alarm()
  - Safe state accessors and timers

### Storage & Configuration
- **storage_mgr.h** (11 functions + 4 enum converters)
  - storage_init(), storage_load_all(), storage_save_*()
  - zone_type_from_string(), zone_type_to_string()
  - relay_type_from_string(), relay_type_to_string()

### Hardware Abstraction
- **hal.h** (8 functions)
  - hal_init(), hal_init_zones(), hal_get_zone_state()
  - hal_set_relay(), hal_set_siren(), hal_set_strobe()
- **hal_time.h** (2 functions)
  - hal_time_init(), get_sentinel_time()

### Networking & Communication
- **net.h** (5 functions)
  - init_ethernet_v3(), init_network_hardware()
  - start_sentinel_mqtt(), network_loop_task(), net_req()
- **telegram.h** (2 functions)
  - telegram_send_alert(), telegram_send_cancellation()
- **smtp.h** (3 functions)
  - smtp_alert_all_contacts(), smtp_send_cancellation(), smtp_send_email()
- **noonlight.h** (7 functions)
  - noonlight_create_alarm(), noonlight_log_event()
  - noonlight_send_instructions(), noonlight_sync_people()
  - noonlight_cancel_alarm(), noonlight_sync_task()

### Alerting & Dispatching
- **dispatcher.h** (2 functions)
  - dispatcher_alert(), dispatcher_cancel_alert()
- **payload_manager.h** (4 functions)
  - payload_build_noonlight(), payload_build_telegram()
  - payload_build_smtp(), payload_parse_response()

### Monitoring & User Management
- **monitor.h** (3 functions)
  - monitor_init(), monitor_scan_all(), monitor_process_event()
- **user_mgr.h** (5 functions)
  - users_to_json(), user_add(), user_update()
  - user_drop(), user_list()

### Logging
- **logging.h** (2 functions + 5 macros)
  - logging_set_level() with usage examples
  - LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR macros

---

## Related Documentation (To Be Created)

### API.md (Planned)
REST endpoint reference:
- GET /status, /zones, /config
- POST /arm, /disarm, /config, /relay/:id/toggle
- Error responses and status codes
- Authentication requirements
- Example requests/responses

### MQTT.md (Planned)
Message broker topic reference:
- sentinel/status/heartbeat (30s publish)
- sentinel/state/arm_state (state changes)
- sentinel/alarm/triggered (zone events)
- sentinel/alarm/set (remote commands)
- Payload schemas and examples

### TROUBLESHOOTING.md (Planned)
Common issues and solutions:
- Zone not triggering
- MQTT won't connect
- Telegram alerts failing
- Web UI not accessible
- Memory/performance issues

---

## Document Navigation

```
README.md (Start here!)
├─ Quick overview and features
├─ Hardware pinouts
└─ Links to detailed docs

ARCHITECTURE.md (Deep dive)
├─ System design
├─ Multi-task architecture
├─ State machine flow
└─ Recovery mechanisms

CONFIG.md (Configuration)
├─ File formats
├─ GPIO mapping
├─ Zone types
└─ Troubleshooting

IMPROVEMENTS.md (What changed)
├─ Security fixes
├─ Reliability features
├─ Code quality
└─ Testing guide

Individual .h files (Function reference)
└─ Doxygen comments with @example
```

---

## How to Use This Documentation

### For New Developers
1. Start with **README.md** for project overview
2. Read **ARCHITECTURE.md** for system design
3. Read **CONFIG.md** for GPIO and configuration
4. Review function docs in header files via IDE autocomplete

### For Integration/Support
1. Use **CONFIG.md** for configuration and troubleshooting
2. Reference **README.md** for feature capabilities
3. Check **IMPROVEMENTS.md** for change history

### For System Architects
1. Study **ARCHITECTURE.md** for complete design
2. Review **IMPROVEMENTS.md** for reliability features
3. Check **CONFIG.md** for extensibility points

### For Code Review
1. Check function docs in header files
2. Review **IMPROVEMENTS.md** for security fixes
3. Verify against **ARCHITECTURE.md** for design compliance

---

## Documentation Standards

### Function Documentation Template
```c
/**
 * @file [filename].h
 * @brief [One-line module purpose]
 * 
 * [Multi-line detailed description]
 */

/**
 * @brief [Function summary]
 * 
 * [Detailed explanation including:
 *  - What it does
 *  - When called
 *  - Side effects
 *  - Integration]
 * 
 * @param param_name [Description with constraints]
 * @return [Type and meaning of return value]
 * 
 * @example
 * // Typical usage
 * result = function_name(param);
 */
type function_name(param_type param);
```

### Markdown Document Structure
- H1: Document title
- H2: Major sections
- Code blocks: Syntax highlighted examples
- Tables: For reference data
- Lists: For step-by-step instructions
- Links: Cross-references within project

---

## Generating HTML Documentation

### Doxygen (C/C++ API docs)
```bash
# Install Doxygen
sudo apt-get install doxygen graphviz

# Generate HTML docs
cd /home/kjgerhart/sentinel-esp
doxygen Doxyfile

# Open in browser
firefox build/html/index.html
```

### Markdown to HTML (for guides)
```bash
# Convert individual .md files to HTML
pandoc README.md -o README.html --standalone --css=style.css
pandoc ARCHITECTURE.md -o ARCHITECTURE.html --standalone

# Or use GitHub's rendering: Push to GitHub repo
```

---

## Quality Metrics

| Aspect | Coverage | Status |
|--------|----------|--------|
| Function docs | 60+ functions | ✅ Complete |
| Architecture doc | Full system design | ✅ Complete |
| Configuration reference | All settings | ✅ Complete |
| Troubleshooting | 5+ common issues | ✅ Complete |
| Code examples | Per function | ✅ 80% complete |
| Deployment checklist | 20+ items | ✅ Complete |
| Performance metrics | All critical paths | ✅ Complete |

---

## Maintenance

### Keeping Documentation Updated
1. **Function changes**: Update Doxygen comments immediately
2. **Architecture changes**: Update ARCHITECTURE.md with impact analysis
3. **Configuration changes**: Update CONFIG.md and example configs
4. **New features**: Add to IMPROVEMENTS.md change history
5. **Bugs fixed**: Document solution approach in IMPROVEMENTS.md

### Review Checklist
- [ ] Doxygen comments compile without warnings
- [ ] All public functions have @brief and @param
- [ ] Examples are syntactically correct
- [ ] Configuration files are valid JSON
- [ ] Links to other documents work
- [ ] No outdated version numbers
- [ ] GPIO mappings match hardware
- [ ] State machine diagrams accurate

---

## Feedback & Contributions

For documentation improvements:
1. Check existing docs for accuracy
2. Add missing examples or clarifications
3. Update outdated information
4. Improve organization or readability
5. Submit feedback on coverage

---

**Documentation Version**: 1.0  
**Last Updated**: 2024  
**Completeness**: ~90% (4 guides complete, API/MQTT/Troubleshooting guides planned)  
**Maintenance**: Actively maintained with code changes

