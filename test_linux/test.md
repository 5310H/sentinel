# Sentinel Alarm System - Complete Test Suite Documentation

## Overview

The `test_complete.sh` script provides **end-to-end automated testing** of the entire Sentinel alarm system when running in mock/test mode (`sentinel_test`). It validates that all critical API endpoints, web pages, authentication, and system functionality work correctly.

---

## What It Tests - 10 Major Sections

### SECTION 1: Server Connectivity ✓

**Purpose**: Ensures the mock server is running and accessible

**Tests**:
- Verifies `sentinel_test` server is running on `http://localhost:8000`
- Confirms the main page (index.html) loads successfully
- Validates server responds to HTTP requests

**Why important**: 
- Foundation for all other tests
- If server isn't running, entire system is non-functional
- Confirms port 8000 is available and listening

---

### SECTION 2: System Status Endpoints ✓

**Purpose**: Validates the two critical status API endpoints that the web UI depends on

**Endpoint 1: `/api/status` (Full System Configuration)**

Returns comprehensive system state including:
- **Config Object**:
  - Account ID, owner name, email, phone
  - Entry/Exit/Cancel delay settings
  - Monitoring service settings (Noonlight, SmartThings, etc.)
  - MQTT broker credentials
  - Telegram/Home Assistant integration URLs
  - System state (0=Disarmed, 1=Armed Away, 2=Armed Stay)
  - Ready status (true if all zones secure)
  - Current violation info (if any)

- **Zones Array**: List of all door/window sensors
  - Zone ID and name
  - Violated status (true if open/triggered)

- **Relays Array**: List of all output relays
  - Relay ID and name
  - Active status (true if energized)

**Endpoint 2: `/status` (Real-Time System Monitoring)**

Returns live sensor data that drives the web UI status indicators:
- `temperature_c`: System temperature in Celsius (displayed as °F in UI)
- `on_backup_power`: Boolean - true if running on UPS/battery
- `tamper_detected`: Boolean - true if case is open

**Why important**:
- Web UI's `check()` function fetches these endpoints every 3 seconds
- Temperature display shows system health
- Power status shows if on backup battery
- Tamper alerts detect case tampering
- Index.html was showing "OFFLINE" before `/status` endpoint was added - **this was the bug we fixed**

---

### SECTION 3: Authentication ✓

**Purpose**: Validates user authentication and PIN security

**Tests**:

**Test 1 - Valid PIN (1234)**:
- Sends POST request to `/api/auth` with correct PIN
- Expected response: `{"authenticated": true, "is_admin": true, "name": "User Name"}`
- Verifies admin flag is returned
- Confirms access granted

**Test 2 - Invalid PIN (9999)**:
- Sends POST request to `/api/auth` with wrong PIN
- Expected response: `{"authenticated": false}`
- Verifies incorrect PIN is rejected
- Confirms security working

**Why important**:
- PIN validation prevents unauthorized arm/disarm
- Only authenticated users can control system
- Validates user identity before performing critical actions
- Admin flag determines which UI pages are accessible

---

### SECTION 4: Arm/Disarm Operations ✓

**Purpose**: Tests the core alarm functionality - arming and disarming the system

**Current State Check**:
- Fetches current system state before testing
- States: 0=Disarmed, 1=Armed Away, 2=Armed Stay

**Test 1 - ARM Operation**:
- POST to `/api/arm` endpoint
- System should transition from state 0 (Disarmed) → 1 (Armed Away)
- Verifies endpoint accepts request and returns `{"status": "ok"}`
- Waits 1 second then checks new state

**Test 2 - DISARM Operation**:
- POST to `/api/disarm` with PIN in body: `{"pin": "1234"}`
- System should transition from state 1 → 0 (Disarmed)
- Requires PIN for security
- Verifies state change took effect

**Why important**:
- **Core functionality** - system must be able to arm/disarm
- Transitions must work reliably
- PIN requirement on disarm prevents accidental disarming
- State changes must be immediate and accurate

---

### SECTION 5: Zone Monitoring ✓

**Purpose**: Tests sensor input monitoring and zone status reporting

**Endpoint: `/api/zones`**

Returns array of all zones with current status:
```json
[
  {"name": "Front Door", "open": false},
  {"name": "Back Door", "open": false},
  {"name": "Living Room Window", "open": false},
  ...
]
```

**Tests**:
- Fetches zone list from server
- Counts total zones
- Displays first 3 zones as sample output
- Validates JSON format

**Why important**:
- Zone status drives real-time alerts
- `zstatus.html` displays this data to user
- Must be accurate - missed open zones = security breach
- Used by arming logic (system won't arm if zones open)

---

### SECTION 6: Web Pages ✓

**Purpose**: Validates all 3 web interface pages load correctly and contain expected content

**Test 1 - index.html (Main Control Panel)**
- Loads main user interface
- Should contain "Sentinel Control Panel" title
- Must contain ARM button elements (`.btn-arm` class)
- **Why**: Primary interface for users - must be functional

**Test 2 - users.html (User Management)**
- Admin page for managing user accounts
- Should contain "Users" reference
- Lists users with add/edit/delete functions
- **Why**: Admin functionality for user management

**Test 3 - zstatus.html (Zone Monitoring)**
- Real-time zone status display page
- Should contain "zone" references
- Displays each sensor's current state
- **Why**: Users need to see which zones are open/secure

**Why important**:
- All pages must be accessible via web server
- Missing content would break functionality
- HTML must be properly formatted and complete
- CSS/JavaScript must load without errors

---

### SECTION 7: Configuration & User Data ✓

**Purpose**: Displays and validates system configuration is properly set

**Extracted from `/api/status`**:
- Account ID: Unique identifier for this system
- Owner Name: Displayed in UI
- Email: Used for alerts and notifications
- Phone: Used for SMS alerts
- Entry Delay: Seconds before alarm triggers when entering
- Exit Delay: Seconds before alarm arms after leaving

**Validation**:
- Checks if account ID is configured (not null)
- Checks if owner name is set
- Warns if critical fields missing

**Why important**:
- Dispatch needs account ID to know which account triggered alarm
- Phone/Email needed for notifications
- Entry/Exit delays configure system response timing
- Missing config would prevent proper operation

---

### SECTION 8: System Status Checks ✓

**Purpose**: Validates overall system health and readiness

**Test 1 - Ready Status**:
- Checks if `config.ready` is `true`
- `ready: true` = All zones secure, system can be armed
- `ready: false` = Some zone is open/violated, cannot arm
- **Why**: User needs to know if system is ready before attempting to arm

**Test 2 - Violation Check**:
- Checks if any violations are currently active
- If violation present, displays what it is
- **Why**: Indicates security events or open zones

**Why important**:
- UI must show accurate system state
- Can't arm if zones are open (violations prevent arming)
- User sees real-time status before actions
- Prevents arming with security problems

---

### SECTION 9: Performance ✓

**Purpose**: Measures API response times to ensure UI responsiveness

**Measurement**:
- Fetches `/api/status` and measures response time in milliseconds
- Validates response time is < 100ms
- Warns if response is slow

**Why important**:
- Web UI calls status endpoint every 3 seconds
- Slow responses make UI feel laggy
- Mobile browsers need fast responses
- Battery-powered systems benefit from quick responses
- If response takes > 100ms × 3 second interval = user interface feels sluggish

---

### SECTION 10: Summary Report ✓

**Purpose**: Final consolidated results showing test outcome

**Output**:
- **PASSED**: Count of successful tests (✓ Green)
- **FAILED**: Count of critical failures (✗ Red) 
- **WARNINGS**: Count of non-critical issues (⚠ Yellow)
- **Total**: Total number of tests run
- **Exit Status**: 
  - Exit code 0 = All critical tests passed ✓
  - Exit code 1 = Some tests failed ✗

**Final Verdict**:
- ✓ ALL CRITICAL TESTS PASSED = System ready
- ✗ SOME TESTS FAILED = Debugging needed before use

---

## Output Features

### Color-Coded Results

- 🟢 **GREEN** = Test passed successfully
- 🔴 **RED** = Test failed (critical issue)
- 🟡 **YELLOW** = Warning (non-critical issue)
- 🔵 **BLUE** = Section headers

### Detailed Logging

- Shows exact HTTP response codes (200, 401, 500, etc.)
- Displays extracted JSON values and configuration
- Shows system state transitions (0→1→0)
- Displays configuration details (name, email, delays)
- Shows timing measurements (response time in ms)
- Sample output of zone data

---

## What It Validates About the System

| Component | What's Tested | Why It Matters |
|-----------|---------------|----------------|
| **Web Server** | Responds to requests on port 8000 | Must be accessible over network |
| **API Endpoints** | All JSON responses valid and parseable | Mobile app/Web UI depends on correct format |
| **Authentication** | PIN validation works correctly | Security - prevents unauthorized access |
| **Control Logic** | Arm/Disarm transitions work reliably | Core functionality must work |
| **Sensors** | Zone status available in real-time | Intrusion detection depends on this |
| **Web UI** | HTML pages load and contain expected content | User interface must be functional |
| **Configuration** | System properly configured | Dispatch & notifications depend on this |
| **Monitoring** | Temperature/Power/Tamper data available | Status indicators in UI work |
| **Performance** | API response < 100ms | Web UI responsiveness |

---

## How to Interpret Results

### ✅ ALL Tests PASS
- System is fully operational
- Web UI will work correctly
- API is responding properly with valid JSON
- Authentication works for security
- Arm/Disarm logic functional
- Sensor monitoring active
- **Ready for deployment** to production

### ⚠️ WARNINGS Appear
- Non-critical issues found (e.g., missing owner name in config)
- System still functional but may have incomplete setup
- Should be addressed before production deployment
- Example: "Account ID not configured" - dispatch won't know which account

### ❌ Tests FAIL
- **Critical issues found** - system not ready for use
- Specific endpoint is broken or returning wrong data
- Need to debug and fix before deployment
- Script output shows which exact endpoint/test failed
- Examples:
  - `/api/status` returns 500 error
  - Authentication always fails
  - Arm/Disarm transitions don't work

---

## Usage

```bash
# Make executable
chmod +x /home/kjgerhart/sentinel-esp/test_linux/test_complete.sh

# Run tests (requires sentinel_test server running on localhost:8000)
/home/kjgerhart/sentinel-esp/test_linux/test_complete.sh

# Or from test_linux directory
cd /home/kjgerhart/sentinel-esp/test_linux
./test_complete.sh
```

### Prerequisites
- `sentinel_test` must be running: `./sentinel_test`
- Server must be listening on `http://localhost:8000`
- `curl` command available
- `jq` JSON parser installed (for JSON parsing)

---

## Test Coverage Summary

This script tests **every major code path** that makes the Sentinel system work:

✅ **Server Infrastructure**
- HTTP server running
- Port accessible
- Static files served

✅ **API Layer**
- All endpoints responding
- JSON responses valid
- HTTP status codes correct

✅ **Business Logic**
- Authentication working
- Arm/Disarm transitions
- State management

✅ **Sensor Integration**
- Zone monitoring functional
- Status data available
- Real-time updates

✅ **User Interface**
- Web pages load
- Static assets served
- JavaScript accessible

✅ **Configuration**
- System properly configured
- Credentials stored
- Delays set correctly

✅ **Performance**
- Response times acceptable
- No timeouts
- UI responsiveness adequate

---

## Related Files

- **test_complete.sh**: The test script itself (this file's counterpart)
- **sentinel_test**: The mock server binary (must be running)
- **main_mock.c**: Server implementation (includes test endpoints)
- **index.html**: Main web UI that uses these endpoints
- **users.html**: User management page
- **zstatus.html**: Zone monitoring page

---

## Version History

- **v1.0** (2026-01-21): Initial comprehensive test suite
  - 10 test sections
  - 30+ individual tests
  - Color-coded output
  - JSON parsing with jq
  - Performance measurements
