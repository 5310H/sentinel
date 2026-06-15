# Sentinel-ESP Changelog

All notable changes to the Sentinel-ESP project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added - 2026-01-31

#### Camera Live Grid View (Complete)

- **Live Grid Interface** (`data/cameras_live.html` 784 lines) - 2026-01-31
  - Multi-camera monitoring dashboard with 1×1, 2×2, 3×3, 4×4 grid layouts
  - Auto-refresh system: 1 FPS for grid view, 2 FPS for fullscreen
  - MJPEG streaming support with automatic fallback to snapshot polling
  - Per-camera error tracking and graceful degradation
  - Professional dark theme with smooth animations
  - Keyboard shortcuts: 1-4 (layouts), R (refresh), M (MJPEG toggle), F (fullscreen), ESC (close)

- **Fullscreen Camera View** - 2026-01-31
  - Click-to-expand any camera to fullscreen
  - High-speed refresh (2 FPS) for detailed inspection
  - Snapshot download functionality (timestamped JPEG files)
  - Quick actions: snapshot, record (planned), settings, back to grid
  - Native browser fullscreen support (F key)
  - Automatic grid refresh pause during fullscreen viewing

- **Grid Layout Controls** - 2026-01-31
  - Dynamic layout switching without page reload
  - Empty slot placeholders for unused grid positions
  - Responsive design for mobile and tablet devices
  - Mobile layout optimization (auto-collapse to smaller grids)
  - Hover effects and visual feedback on camera tiles

- **Status Monitoring** - 2026-01-31
  - Live status indicators with pulsing green dot for online cameras
  - Offline detection with red dot and grayed-out appearance
  - MJPEG vs snapshot mode indicators
  - Info banners for mode switches and warnings
  - Camera name and hostname overlay on each tile

- **Performance Optimizations** - 2026-01-31
  - Cache-busting timestamps on snapshot URLs
  - Memory-safe blob management for downloads
  - Interval cleanup on page unload
  - Grid refresh pause during fullscreen (reduces load)
  - Adaptive refresh rates (1 FPS grid, 2 FPS fullscreen)

- **Documentation** (`CAMERA_GRID_IMPLEMENTATION.md` ~700 lines) - 2026-01-31
  - Comprehensive feature documentation
  - Usage guide with keyboard shortcuts
  - Troubleshooting section
  - Performance characteristics and scalability analysis
  - Future enhancement roadmap (MJPEG endpoint, recording, PTZ)

#### Task Scheduler Component (Complete)

- **Scheduler Core Implementation** (`components/scheduler/scheduler.c` ~650 lines) - 2026-01-31
  - Full cron expression parser with bitmask matching (minute, hour, day, month, weekday)
  - Interval tasks (run every N seconds)
  - One-shot tasks (run once at specific time)
  - 4 priority levels (LOW, NORMAL, HIGH, CRITICAL)
  - 64 max concurrent tasks (configurable)
  - Task enable/disable/delete/trigger operations
  - Execution tracking (last_run, run_count, error_count)
  - Platform support: ESP32 (esp_timer) and Linux (pthread)

- **Scheduler API** (`components/scheduler/scheduler.h` 286 lines) - 2026-01-31
  - `scheduler_init/deinit/start/stop` - Lifecycle management
  - `scheduler_add_interval_task()` - Every N seconds scheduling
  - `scheduler_add_cron_task()` - Cron expression scheduling
  - `scheduler_add_once_task()` - One-shot time-based execution
  - `scheduler_parse_cron()` - Parse cron string to bitmask structure
  - `scheduler_cron_matches()` - Check if cron matches current time
  - `scheduler_cron_next()` - Calculate next run time
  - Comprehensive documentation with cron examples

- **Zigbee Automation Integration** (`components/zigbee/zigbee_automations.c`) - 2026-01-31
  - Replaced ad-hoc `esp_timer` with `scheduler_add_interval_task()`
  - 60-second interval task for time-based automation triggers
  - Cleaner code with professional task management
  - Improved maintainability and extensibility

- **Build System Updates** - 2026-01-31
  - Added `components/scheduler/CMakeLists.txt` with esp_timer dependency
  - Updated `components/zigbee/CMakeLists.txt` with scheduler dependency
  - Updated `main/CMakeLists.txt` with scheduler dependency
  - Integrated scheduler initialization in `main/main.c` app_main()

- **REST API Endpoints** (`main/main.c` lines 1807-1968) - 2026-01-31
  - GET /api/scheduler/tasks - List all scheduled tasks
  - POST /api/scheduler/tasks - Create interval/cron/one-shot tasks
  - PUT /api/scheduler/tasks/:id - Enable/disable task
  - DELETE /api/scheduler/tasks/:id - Delete task
  - POST /api/scheduler/tasks/:id/trigger - Manual task execution
  - Full JSON request/response handling
  - Task details include type, priority, status, run counts, timestamps

- **Web UI** (`data/scheduler.html` ~700 lines) - 2026-01-31
  - Stats dashboard showing total tasks and breakdown by type
  - Task table with sortable columns (name, type, schedule, priority, status, runs, timestamps)
  - Create task modal with form wizard for all schedule types
  - Per-task actions: enable/disable, manual trigger, delete
  - Real-time refresh capability
  - Responsive design with gradient theme matching Sentinel UI
  - Badge system for types, priorities, and status
  - Empty state handling

- **Navigation Integration** (`data/index.html`) - 2026-01-31
  - Added "⏰ Scheduler" link to admin menu
  - Positioned between Zigbee Devices and ZStatus

- **Documentation** (`SCHEDULER_IMPLEMENTATION_COMPLETE.md`) - 2026-01-31
  - Complete API reference with 20+ functions
  - Cron syntax guide and examples
  - Usage examples for interval, cron, and one-shot tasks
  - Integration examples with zigbee automations
  - Platform-specific implementation details
  - REST API endpoint documentation
  - Web UI feature overview

#### Password Hashing and Session Token System (Sprint 1 Complete)

- **Password Authentication Integration** (`components/storage/storage_mgr.c` lines 947-1142) - 2026-01-31
  - `user_authenticate_pin()` - Unified authentication supporting both hashed and legacy PINs
  - `user_migrate_to_hash()` - Migrate single user from plaintext to PBKDF2-HMAC-SHA256
  - `user_set_password()` - Set new hashed password for user
  - `users_migrate_all_to_hash()` - Batch migrate all users on startup
  - Automatic migration on first boot with backward compatibility
  - Uses existing password_hash.c infrastructure (10000 iterations, 32-byte salt)

- **JWT Session Token API** (`main/main.c` lines 570-603) - 2026-01-31
  - POST `/api/login` - Authenticate with PIN, returns JWT token
  - Returns: token, username, is_admin flag
  - Session tied to client IP address for security
  - 1-hour token expiration (configurable)
  - HMAC-SHA256 signature verification
  - Uses existing session_mgr.c infrastructure

- **User Structure Updates** (`components/storage/storage_mgr.h` lines 117-131) - 2026-01-31
  - Added `password_hash_t password_hash` - Binary PBKDF2 hash storage
  - Added `bool use_legacy_pin` - Migration flag for dual-mode auth
  - Maintains backward compatibility with existing pin_hash hex format
  - Automatic detection of legacy vs new format on load

- **Automatic Migration on Startup** (`main/main.c` lines 1031-1041) - 2026-01-31
  - Initialize session manager on boot
  - Scan all users for plaintext PINs
  - Migrate to hashed format automatically
  - Persist changes to users.json
  - Log migration results (success/failure counts)

- **Engine Authentication Refactor** (`components/engine/engine.c` lines 201-227) - 2026-01-31
  - Replaced manual PIN comparison loop with `user_authenticate_pin()`
  - Simplified authentication logic (40 lines → 27 lines)
  - Unified handling for legacy and hashed PINs
  - Enhanced audit logging with authentication method

- **Linux Test Support** (`test_linux/main_mock.c`) - 2026-01-31
  - Updated `__wrap_engine_check_keypad()` to use unified authentication
  - Added POST `/api/login` endpoint for token creation
  - Added migration on startup (lines 898-904)
  - Full parity with ESP32 implementation

- **Function Prototypes** (`components/storage/storage_mgr.h` lines 406-440) - 2026-01-31
  - Complete documentation for password management functions
  - Migration workflow examples
  - Return value specifications

### Changed - 2026-01-31

- **User Loading** (`components/storage/storage_mgr.c` lines 556-653) - 2026-01-31
  - Initialize `password_hash` struct to zeros
  - Detect hex vs binary hash format
  - Set `use_legacy_pin` flag based on format
  - Convert old hex format to binary on load
  - Log warnings for users needing migration

### Security - 2026-01-31

- **Sprint 1 Security Hardening Complete**
  - Password hashing: PBKDF2-HMAC-SHA256 with 10000 iterations
  - Session tokens: JWT with HMAC-SHA256 signatures
  - Constant-time password comparison
  - Hardware RNG for salt generation (ESP32)
  - IP-based session binding
  - Automatic session cleanup
  - 5-minute lockout after 5 failed attempts
  - Audit logging for all authentication events

---

### Added - 2026-01-30

#### ESPHome Device Management Web UI
- **Device Management Page** (`data/devices.html`) - 2026-01-30
  - Full-featured web UI for managing ESPHome devices
  - Add/Edit/Delete devices with form validation
  - Real-time device list with status indicators
  - Configure: hostname, port, credentials, encryption, virtual zones/relays
  - Responsive design matching Sentinel theme
  - Authentication check (requires login)
  - Success/error message notifications

- **Navigation Update** (`data/index.html` line 109) - 2026-01-30
  - Added "Devices" link to admin menu
  - Positioned between Users and ZStatus

#### ESPHome Device CRUD API
- **ESPHome Device Management Functions** (`components/storage/storage_mgr.c` lines 812-921) - 2026-01-30
  - `esphome_devices_to_json()` - Serialize all devices to JSON array
  - `esphome_device_add()` - Add new ESPHome device with validation
  - `esphome_device_update()` - Update existing device by hostname
  - `esphome_device_delete()` - Remove device and shift array
  - `esphome_devices_save()` - Persist changes to esphome.json file

- **REST API Endpoints** (`main/main.c` lines 208-297) - 2026-01-30
  - GET `/api/esphome` - List all configured ESPHome devices
  - POST `/api/esphome/add` - Add new device with configuration
  - POST `/api/esphome/update` - Update device settings (hostname is key)
  - POST `/api/esphome/delete` - Remove device by hostname
  - Supports: hostname, port, friendly_name, password, encryption_key, virtual zones/relays, enabled flag

- **Function Prototypes** (`components/storage/storage_mgr.h` lines 303-363) - 2026-01-30
  - Complete documentation for all ESPHome management functions
  - Parameter descriptions and return values
  - Usage examples in doc comments

- **Linux Test Support** (`test_linux/main_mock.c` lines 751-843) - 2026-01-30
  - Mock endpoints for ESPHome CRUD testing
  - Full parity with ESP32 implementation

### Fixed - 2026-01-30

- **Arming Mode Selection:** /api/arm endpoint now parses and respects mode parameter (away/stay/night/vacation)
- **VACATION Mode:** Added ARM_VACATION to arm_mode_t enum with full implementation
- **State Display:** Fixed duplicate case statement, display now shows current arm mode during EXITING/ARMED states
- **Audit Logging:** State change logs now include arm mode (AWAY/STAY/NIGHT/VACATION)
- **MQTT Integration:** ha_mqtt.c now supports vacation mode command
- **Memory Safety:** Fixed null pointer crashes in esphome_devices_to_json() and CRUD functions
- **Input Validation:** Added hostname/friendly_name validation in ESPHome device add/update/delete
- **Buffer Overflow Prevention:** Added encryption key length validation and explicit null termination
- **HTTP Security:** All ESPHome API endpoints validate required fields and return proper error codes

## Fixed - 2026-01-30

#### Security Hardening - Memory & Input Validation
- **Memory Leak Fix** (`components/storage/storage_mgr.c:817`) - Added null check for cJSON_CreateObject to prevent memory leak in esphome_devices_to_json()
- **Null Pointer Protection** (`components/storage/storage_mgr.c:833,860,888`) - Added input validation for hostname/friendly_name parameters in all CRUD functions
- **HTTP Input Validation** (`main/main.c:217,249,282`) - Added required field validation for all ESPHome API endpoints with proper 400 error responses
- **Buffer Overflow Prevention** (`components/esphome_api/esphome_api_client.c:112`) - Added explicit null termination and length checking for encryption key in esphome_api_connect()
- **Security Review** - Complete audit documented in SECURITY_REVIEW_2026-01-30.md (8 vulnerabilities found and fixed)

### Changed - 2026-01-30
- **Storage Manager Header** (`components/storage/storage_mgr.h` line 18) - Added `#include <stdint.h>` for uint16_t support
- **ESPHome Device Type** (`components/storage/storage_mgr.h` lines 163-172) - Added runtime state fields to esphome_device_t (is_connected, use_encryption, socket_fd, mac_address, esphome_version, last_ping_ms)
- **ESPHome API Client** (`components/esphome_api/esphome_api_client.c`) - Refactored to use global esphome_devices[] array from storage_mgr instead of local device registry
- **ESPHome API Header** (`components/esphome_api/esphome_api_client.h`) - Removed duplicate esphome_device_t typedef, now uses definition from storage_mgr.h

### Added - 2026-01-29

#### Secure OTA Firmware Updates
- **OTA Upload Endpoint** (`main/main.c` line 490-680) - 2026-01-29
  - POST `/api/ota/upload` - Secure firmware update endpoint
  - PIN authentication (master PIN or user PIN via Bearer token)
  - Firmware validation: ESP32 magic number (0xE9) and image header checks
  - Atomic updates with automatic rollback protection
  - 10MB max firmware size with partition space verification
  - Auto-restart after successful flash (3 second delay)
  - HTTPS encryption (TLS 1.2+) for all uploads

- **OTA State Management** (`main/main.c` line 53-56) - 2026-01-29
  - Global OTA handle and partition tracking
  - Upload progress monitoring
  - In-progress flag for safety checks

- **OTA Dependencies** (`main/main.c` line 17-19) - 2026-01-29
  - `esp_ota_ops.h` - OTA operations API
  - `esp_app_format.h` - Firmware format validation
  - `mbedtls/sha256.h` - Cryptographic hashing support

- **Documentation** - 2026-01-29
  - `OTA_SECURE_UPDATE_GUIDE.md` - Comprehensive 400+ line guide
    - Security architecture and authentication flow
    - Three update methods (curl, web UI, CI/CD)
    - Troubleshooting guide with common errors
    - Production deployment checklist
    - Future enhancements (firmware signing, differential updates)
  - `OTA_QUICK_REFERENCE.md` - One-page cheat sheet
    - Quick update command with examples
    - Common error codes and fixes
    - Emergency rollback procedures

### Added - 2026-01-28 (14:30 UTC)

#### Master PIN (config.pin) Authentication
- **Step 1 PIN Validation Flow** (`test_linux/main_mock.c`, `test_linux/index.html`, `data/index .html`) - 2026-01-28 14:15 UTC
  - Validates entered PIN against `config.pin` first
  - If match: Returns `requires_totp: false` for immediate access (no 2FA)
  - If no match: Falls through to user database lookup
  - Users with 2FA configured: `requires_totp: true` → TOTP step
  - Users without 2FA: `requires_totp: false` → Grant access

- **HTML Authentication Handler Simplified** - 2026-01-28 14:22 UTC
  - Removed forced 2FA setup requirement
  - Single logic: if `requires_totp === true` → TOTP step, else → grant access
  - Master PIN (config.pin) bypasses 2FA entirely
  - Email sending for 2FA setup removed (not required for master PIN)

- **Email Endpoint** (`test_linux/main_mock.c`, `main/main.c`) - 2026-01-28 14:05 UTC
  - Validates that user is master/system user (config.name)
  - Uses config.email for master user email delivery
  - Returns 403 if non-master user attempts to use endpoint
  - Returns 404 if no email configured

#### Security Hardening (Phase 1) - 2026-01-27 16:00 UTC
- **Password Hashing Component** (`components/security/password_hash.{h,c}`)
  - PBKDF2-HMAC-SHA256 with 10,000 iterations
  - 256-bit random salt per user (hardware RNG)
  - 256-bit hash output
  - Constant-time comparison to prevent timing attacks
  - mbedTLS for ESP32, OpenSSL for Linux testing
  - 137-byte hex storage format

- **Session Management Component** (`components/security/session_mgr.{h,c}`)
  - JWT (JSON Web Token) implementation
  - HMAC-SHA256 signatures
  - 1-hour token expiration
  - IP address binding (optional)
  - 8 concurrent sessions (reduced from 16 for memory optimization)
  - 256-bit secret key stored in NVS
  - Automatic cleanup of expired sessions

- **Audit Logging Component** (`components/security/audit_log.{h,c}`)
  - Circular buffer with 100 entries (reduced from 1000 for memory optimization)
  - SPIFFS persistence (`/spiffs/audit.log`)
  - Auto-save every 10 entries
  - JSON export via `/api/audit` endpoint
  - Tracks: login success/failure, arm/disarm, user changes, config changes
  - Forensic trail with sequence numbers, timestamps, user, IP, action, result

- **User Data Structure Enhancement** (`components/storage/storage_mgr.h`)
  - Added `pin_hash[137]` field for hashed PINs
  - Added `emergency_pin[STR_SMALL]` field for Noonlight dispatch
  - Retained `pin[STR_SMALL]` for backward compatibility during migration

- **Authentication Updates** (`components/engine/engine.c`)
  - Hybrid authentication: tries hashed PIN first, falls back to plaintext
  - Audit logging for all authentication events
  - Failed attempt counter with details
  - Lockout tracking (5 attempts, 60s lockout)
  - State transition logging (DISARMED → ARMED_AWAY, etc.)

- **User Management Updates** (`components/engine/user_mgr.c`)
  - `user_add()`: Automatically hashes PINs on creation
  - `user_update()`: Hashes new PINs, clears legacy plaintext
  - Migration support for existing users

- **Storage Updates** (`components/storage/storage_mgr.c`)
  - `storage_save_users()`: Saves both `pin_hash` and `emergency_pin`
  - User loading: Parses new security fields from JSON
  - Backward compatible with pre-security `users.json` files

- **Test Environment Integration** (`test_linux/`)
  - Updated Makefile with security component objects
  - Added OpenSSL linking (`-lssl -lcrypto`)
  - Initialized security managers in `main_mock.c`
  - Added 3 test API endpoints:
    - `POST /api/test/hash` - Test password hashing
    - `POST /api/test/session` - Test JWT creation/validation
    - `GET /api/audit` - Export audit log

- **Build System Updates**
  - Created `components/security/CMakeLists.txt`
  - Added security component to root `CMakeLists.txt`
  - Memory optimizations to fit within ESP32 RAM constraints

#### API Compliance & Bug Fixes
- **Noonlight API Fixes** (`components/engine/noonlight.c`)
  - Changed `noonlight_cancel_alarm()` from PATCH to POST (API spec compliance)
  - Changed status value from `"canceled"` to `"CANCELED"` (uppercase per spec)
  - Fixed `noonlight_send_instructions()` to use nested `instructions[]` array structure
  - Updated `noonlight_sync_people()` to use `emergency_pin` instead of login PIN
  - Improved security: login PINs no longer sent to third-party services

#### UI/UX Improvements
- **System Monitoring Dashboard** (`data/index .html`, `test_linux/index.html`)
  - Added **Power Status** indicator (AC/BATTERY with orange alert)
  - Added **Tamper Detection** indicator (OK/ALERT with red alert)
  - Removed MAC address indicator (cleaner 4-column layout)
  - Dynamic updates every 4 seconds from `/status` endpoint
  - Visual alerts with background color changes

- **Display Formatting** (`main/main.c`)
  - Fixed escaped quotes in OLED display strings
  - State text now displays correctly: "DISARMED", "ARMED AWAY", etc.

#### Documentation
- **SECURITY_IMPLEMENTATION.md** (NEW)
  - Comprehensive 400+ line security documentation
  - Component details and architecture
  - Integration points and modified files
  - Testing procedures and commands
  - Memory usage and optimization notes
  - Security considerations and future enhancements
  - Troubleshooting guide
  - Compliance notes (NIST, OWASP)

- **README.md**
  - Added "Security" section with 5 security features
  - Updated connectivity section (HTTPS on port 443)

- **QUICK_REFERENCE.md**
  - Added "Security Components" quick reference
  - Password hashing specs
  - Session token details
  - Audit log information

#### Configuration Updates
- **users.json** (`data/users.json`)
  - Added `emergency_pin` field for both Admin and User1
  - Example format for future user additions

### Changed - 2026-01-28

#### Memory Optimizations
- Reduced audit log entries: 1000 → 100 (saved ~225 KB RAM)
- Reduced session token size: 512 → 384 bytes
- Reduced max concurrent sessions: 16 → 8
- Total memory savings: ~230 KB to fit ESP32 DRAM constraints

#### Compiler Fixes
- Added `#include <inttypes.h>` to security components
- Fixed format specifiers: `%u` → `PRIu32` for `uint32_t` types
- Fixed format specifiers: `%x` → `PRIx32` for hex output
- Fixed format specifiers: `%u` → `SCNu32` for scanf
- Updated mbedTLS function: `mbedtls_pkcs5_pbkdf2_hmac` → `mbedtls_pkcs5_pbkdf2_hmac_ext`
- Added pragma directives to suppress false-positive truncation warnings
- Increased alarm message buffer: 32 → 80 bytes
- Fixed display update function declaration placement

#### System Architecture
- **System Monitoring Init** (`components/engine/system_monitoring.c`)
  - Updated signature: `system_monitoring_init(void*)` to accept I2C bus handle
  - Fixed `_i2c_init()` to accept bus handle parameter
  - Updated callers to pass `NULL` or valid handle

- **Engine Init** (`components/engine/engine.c`)
  - Updated `system_monitoring_init()` call to pass `NULL`
  - Moved external function declarations to proper scope

- **Logging** (`components/engine/logging.c`)
  - Removed unused `level_names` variable

### Fixed - 2026-01-28

#### Security Vulnerabilities
- ❌ **Plaintext PIN storage** → ✅ Hashed with PBKDF2-SHA256
- ❌ **No session management** → ✅ JWT tokens with 1-hour expiration
- ❌ **No audit trail** → ✅ Forensic logging to SPIFFS
- ❌ **PINs sent to Noonlight** → ✅ Separate emergency PIN field
- ❌ **Timing attack vulnerability** → ✅ Constant-time comparison

#### Build Errors
- Fixed "region dram0_0_seg overflowed by 43440 bytes" error
- Fixed implicit function declaration warnings
- Fixed format string warnings (all `-Werror=format=` errors)
- Fixed missing header includes
- Fixed function signature mismatches
- Fixed stray backslashes in string literals

#### API Compliance
- Fixed Noonlight cancel endpoint (PATCH → POST)
- Fixed Noonlight status value (lowercase → UPPERCASE)
- Fixed Noonlight instructions structure (flat → nested array)

---

## [Unreleased]

### Added - 2026-02-06

#### ESPHome Cross-Platform Integration (Complete)

- **ESPHome API Client** (`components/esphome_api/` ~800 lines) - 2026-02-06
  - Cross-platform TCP client supporting ESP-IDF (ESP32) and POSIX sockets (Linux)
  - ESPHome Native API protocol implementation (protobuf-based binary protocol)
  - Device discovery and entity enumeration (sensors, switches, lights)
  - Real-time state updates and subscription management
  - Encryption support with configurable keys and passwords
  - Connection pooling and automatic reconnection logic
  - Platform-specific threading: FreeRTOS tasks (ESP32) vs POSIX threads (Linux)

- **Virtual Zone Mapping** (`components/esphome_api/esphome_zones.c` ~250 lines) - 2026-02-06
  - Map ESPHome binary sensors to virtual zones (IDs 33-64)
  - Real device connectivity on Linux (vs mock states in previous versions)
  - Automatic zone state updates from ESPHome device sensors
  - Zone configuration persistence and device association
  - Cross-platform compatibility for development and production

- **Virtual Relay Mapping** (`components/esphome_api/esphome_zones.c` ~150 lines) - 2026-02-06
  - Map ESPHome switches/lights to virtual relays (IDs 32+)
  - Bidirectional control: relay commands sent to ESPHome devices
  - Brightness control for ESPHome light entities
  - Device state synchronization and error handling
  - Integration with existing alarm system relay infrastructure

- **ESPHome Protocol Implementation** (`components/esphome_api/esphome_api_protocol.c` ~260 lines) - 2026-02-06
  - Complete ESPHome Native API message encoding/decoding
  - Hello/Connect handshake with authentication
  - ListEntities, SubscribeStates, and DeviceInfo requests
  - SwitchCommand and LightCommand message support
  - Varint encoding/decoding for protocol efficiency
  - Cross-platform logging with conditional compilation

- **Build System Integration** - 2026-02-06
  - Added `components/esphome_api/CMakeLists.txt` with platform-specific dependencies
  - Updated `Makefile` with ESPHome object files for Linux builds
  - Conditional compilation for ESP_PLATFORM vs POSIX environments
  - Proper include paths and library linking

- **REST API Endpoints** (`main/main.c` lines 2100-2300) - 2026-02-06
  - GET `/api/esphome/devices` - List configured ESPHome devices
  - GET `/api/esphome/status` - Connection status and entity counts
  - POST `/api/esphome/device/add` - Add new ESPHome device configuration
  - POST `/api/esphome/device/connect` - Connect to specific device
  - POST `/api/esphome/device/discover` - Discover entities on device
  - POST `/api/esphome/entity/map_zone` - Map sensor to virtual zone
  - POST `/api/esphome/entity/map_relay` - Map switch/light to virtual relay
  - POST `/api/esphome/entity/control` - Control ESPHome entities

- **Web Management Interface** (`data/esphome.html` ~150 lines) - 2026-02-06
  - ESPHome device configuration and status dashboard
  - Device connection management with real-time status
  - Entity discovery and mapping interface
  - Virtual zone/relay assignment controls
  - Responsive design matching Sentinel UI theme

- **Test Data Configuration** (`test_linux/data/esphome.json` 63 lines) - 2026-02-06
  - Pre-configured ESPHome devices for testing
  - Zone and relay mappings for comprehensive validation
  - Authentication credentials and encryption keys
  - Multiple device scenarios (motion sensors, smart plugs, lights)

#### Comprehensive Test Suite Updates

- **ESPHome API Testing** (`scripts/test_complete.sh` +200 lines) - 2026-02-06
  - New Section 7: ESPHome Device API Tests
  - Device configuration and status endpoint validation
  - Virtual zone/relay mapping functionality tests
  - Entity control and brightness adjustment testing
  - Cross-platform compatibility verification
  - Integration with existing alarm system status endpoints

- **Test Infrastructure** (`test_linux/test_complete.sh` synchronized) - 2026-02-06
  - Updated test script with ESPHome section
  - Comprehensive API endpoint coverage
  - Mock server compatibility for offline testing
  - Performance and reliability validation

- **Documentation Assets** - 2026-02-06
  - ESPHome management page for test environment
  - API endpoint documentation in test suite
  - Cross-platform development workflow validation

### Removed - 2026-02-06

#### Zigbee and Matter Component Removal

- **Zigbee Component Removal** - 2026-02-06
  - Removed `components/zigbee.disabled/` directory completely
  - Added `zigbee.disabled` to `EXCLUDE_COMPONENTS` in `CMakeLists.txt`
  - Commented out Zigbee includes in `main/main.c` (lines 41-45)
  - Disabled Zigbee API endpoints with `#if 0` blocks (lines 1643-2141)
  - Cleaned build cache to remove component references
  - Saved ~50KB DRAM by removing Zigbee protocol stack

- **Matter Component Removal** - 2026-02-06
  - Removed `components/matter_controller/` directory completely
  - Confirmed `matter_controller` already in `EXCLUDE_COMPONENTS`
  - Matter initialization already conditionally disabled in `main/main.c`
  - No Matter API endpoints were implemented
  - Eliminated ESP-Matter SDK dependency requirement

### Fixed - 2026-02-06

#### Partition Table Optimization

- **App Partition Size Increase** - 2026-02-06
  - Updated `main/partitions.csv` to accommodate 828KB binary size
  - Increased app partitions from 640KB to 896KB each (0xA0000 → 0xE0000)
  - Removed `ota_1` partition to fit within 2MB flash constraint
  - Adjusted storage partition size and offset for optimal space usage
  - Maintained OTA capability with factory + ota_0 partitions

- **Build System Stability** - 2026-02-06
  - Resolved partition overflow errors preventing successful builds
  - Optimized flash layout: 896KB factory + 896KB OTA + 192KB storage
  - Maintained backward compatibility with existing OTA update mechanism
  - Preserved SPIFFS storage capacity for configuration and logs

---

## Version History

### [0.9.0] - 2026-01-28
- Security Hardening Phase 1 implementation
- Noonlight API compliance fixes
- System monitoring UI enhancements
- Memory optimizations for ESP32
- Comprehensive security documentation

---

## Migration Notes

### Upgrading to 0.9.0

**User Data Migration:**
- Existing `users.json` files are **fully compatible**
- Users with plaintext PINs will continue to work (automatic migration)
- On next PIN change, users automatically migrate to hashed storage
- Add `emergency_pin` field manually if using Noonlight dispatch

**Configuration Changes:**
- No breaking changes to `config.json`
- No action required for existing deployments

**Build System:**
- Clean build recommended: `idf.py fullclean && idf.py build`
- New dependencies: mbedTLS (PBKDF2, HMAC), NVS (session keys)

**Testing:**
- Test security components: `cd test_linux && make clean && make && ./sentinel_test`
- Verify audit log: `curl http://localhost:8000/api/audit`
- Test password hashing: `curl -X POST http://localhost:8000/api/test/hash -d '{"pin":"1234"}'`

---

## Security Advisories

### 2026-01-28: Password Hashing Implementation
- **Severity:** MEDIUM
- **Impact:** Plaintext PINs replaced with PBKDF2-SHA256 hashes
- **Action Required:** No immediate action - automatic migration on next use
- **Recommendation:** Force password reset for all users to complete migration

### 2026-01-28: Noonlight PIN Leakage Fixed
- **Severity:** LOW
- **Impact:** Login PINs no longer sent to Noonlight API
- **Action Required:** Add `emergency_pin` field to `users.json`
- **Recommendation:** Use separate emergency PIN for dispatcher verification

---

## Contributors

- Development Team
- Security Review Team
- Testing Team

---

## References

- [PBKDF2 Spec - RFC 2898](https://tools.ietf.org/html/rfc2898)
- [JWT Spec - RFC 7519](https://tools.ietf.org/html/rfc7519)
- [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)
- [Noonlight Dispatch API v1](https://docs.noonlight.com)
