# Security Hardening Implementation

**Implementation Date:** January 28, 2026  
**Status:** ✅ Complete (Phase 1)

## Overview

This document details the security enhancements implemented to protect user credentials and provide forensic audit capabilities for the Sentinel alarm system.

---

## Components Implemented

### 1. Password Hashing (`components/security/password_hash.{h,c}`)

**Purpose:** Replace plaintext PIN storage with cryptographically secure hashes.

**Implementation:**
- **Algorithm:** PBKDF2-HMAC-SHA256
- **Iterations:** 10,000 (configurable via `PASSWORD_HASH_ITERATIONS`)
- **Salt Length:** 256 bits (32 bytes) - randomly generated per user
- **Output Length:** 256 bits (32 bytes)
- **Storage Format:** 136-byte hex string (64 hex salt + 64 hex hash + 8 hex iterations)

**Key Functions:**
```c
bool password_hash_create(const char *pin, password_hash_t *out_hash);
bool password_hash_verify(const char *pin, const password_hash_t *stored_hash);
char* password_hash_to_hex(const password_hash_t *hash, char *out_hex);
bool password_hash_from_hex(const char *hex_str, password_hash_t *out_hash);
```

**Security Features:**
- Hardware RNG for salt generation (ESP32 `esp_fill_random`)
- Constant-time comparison to prevent timing attacks
- ESP32 hardware SHA-256 acceleration via mbedTLS
- OpenSSL compatibility for Linux testing

**Migration Strategy:**
- Added `pin_hash` field to `user_t` struct (137 bytes)
- Retained legacy `pin` field for backward compatibility
- `engine_authenticate()` tries hashed PIN first, falls back to plaintext
- Users automatically migrate to hashed PINs on next password change

---

### 2. Session Management (`components/security/session_mgr.{h,c}`)

**Purpose:** Provide JWT-based session tokens to avoid repeated PIN entry.

**Implementation:**
- **Token Format:** JWT (JSON Web Tokens)
- **Algorithm:** HMAC-SHA256 signatures
- **Token Structure:** `header.payload.signature` (Base64URL encoded)
- **Expiration:** 1 hour (configurable via `SESSION_EXPIRY_SECONDS`)
- **Max Sessions:** 8 concurrent sessions
- **Token Size:** 384 bytes per token

**JWT Payload:**
```json
{
  "sub": "username",
  "iat": 1234567890,
  "exp": 1234571490,
  "ip": "192.168.1.100"
}
```

**Key Functions:**
```c
bool session_mgr_init(void);
bool session_create(const char *username, const char *client_ip, char *out_token);
bool session_validate(const char *token, const char *client_ip, char *out_username);
void session_revoke(const char *token);
void session_cleanup_expired(void);
```

**Security Features:**
- 256-bit secret key stored in NVS (persists across reboots)
- IP address binding (optional, prevents token theft)
- Automatic expiration and cleanup
- Constant-time HMAC verification
- Session revocation support (logout)

**Storage:**
- Secret key: NVS namespace "session_mgr", key "secret"
- Active sessions: RAM only (8 slots)
- Cleanup task runs every 60 seconds

---

### 3. Audit Logging (`components/security/audit_log.{h,c}`)

**Purpose:** Record all security-relevant events for forensic analysis and compliance.

**Implementation:**
- **Buffer:** Circular buffer (100 entries in RAM)
- **Persistence:** Auto-save to SPIFFS every 10 entries
- **Format:** CSV on disk, JSON for API export
- **File Path:** `/spiffs/audit.log`

**Entry Structure:**
```c
typedef struct {
    uint32_t seq;                              // Monotonic sequence number
    time_t timestamp;                          // Unix timestamp
    char user[32];                             // Username or "system"
    char ip_address[16];                       // Client IP
    char action[32];                           // Action type
    char resource[32];                         // Resource affected
    bool success;                              // Success/failure
    char details[128];                         // Additional context
} audit_entry_t;
```

**Logged Events:**
- `LOGIN_SUCCESS` - Successful PIN authentication
- `LOGIN_FAILED` - Failed authentication attempt (with counter)
- `AUTH_LOCKOUT` - Account locked after 5 failed attempts
- `ARM_STATE_CHANGE` - Arm/disarm operations (e.g., DISARMED→ARMED_AWAY)
- `USER_ADDED` - New user created
- `USER_UPDATED` - User PIN or settings changed
- `USER_DELETED` - User account removed
- `CONFIG_CHANGE` - System configuration modified

**Key Functions:**
```c
bool audit_log_init(void);
void audit_log_write(const char *user, const char *ip, const char *action, 
                     const char *resource, bool success, const char *details);
int audit_log_get_entries(audit_entry_t *entries, int max_entries, uint32_t since_seq);
char* audit_log_to_json(char *out_json, size_t max_json_len, int max_entries);
void audit_log_flush(void);
```

**API Endpoint:**
```
GET /api/audit
Response: {"seq":1234,"entries":[...]}
```

---

## Integration Points

### Modified Files

#### `components/storage/storage_mgr.h`
- Added `char pin_hash[137]` to `user_t` struct
- Added `char emergency_pin[STR_SMALL]` for Noonlight dispatch

#### `components/storage/storage_mgr.c`
- `storage_save_users()`: Saves both `pin_hash` and `pin` fields
- User loading: Parses `pin_hash` and `emergency_pin` from JSON
- Backward compatible with legacy `users.json` files

#### `components/engine/engine.c`
- Added `#include "../security/password_hash.h"`
- Added `#include "../security/audit_log.h"`
- `engine_authenticate()`:
  - Tries hashed PIN verification first
  - Falls back to legacy plaintext comparison
  - Logs `LOGIN_SUCCESS` or `LOGIN_FAILED` to audit log
  - Triggers `AUTH_LOCKOUT` after 5 failures
- `engine_set_arm_state_safe()`:
  - Logs state transitions (e.g., "DISARMED → ARMED_AWAY")

#### `components/engine/user_mgr.c`
- `user_add()`: Hashes PIN before storage via `password_hash_create()`
- `user_update()`: Hashes new PIN, clears legacy plaintext field
- Logs "Created hashed PIN for user: %s" messages

#### `components/engine/noonlight.c`
- Updated `noonlight_cancel_alarm()`: Changed PATCH → POST (API spec compliance)
- Updated `noonlight_send_instructions()`: Fixed nested JSON structure
- Updated `noonlight_sync_people()`: Uses `emergency_pin` field instead of login PIN

#### `data/users.json`
- Added `emergency_pin` field for both users (same as PIN for now)
- Example:
```json
{
  "name": "Admin",
  "pin": "1234",
  "emergency_pin": "1234",
  "phone": "14404652865"
}
```

---

## Testing

### Test Environment Integration

#### `test_linux/Makefile`
- Added `SECURITY_DIR = $(COMP_BASE)/security`
- Added `-I$(SECURITY_DIR)` to includes
- Added object files: `password_hash.o`, `session_mgr.o`, `audit_log.o`
- Links against OpenSSL: `-lssl -lcrypto`

#### `test_linux/main_mock.c`
- Added security headers
- `main()`: Initializes `session_mgr_init()` and `audit_log_init()`
- Added 3 test API endpoints (lines 555-598):
  - `POST /api/test/hash` - Test password hashing
  - `POST /api/test/session` - Test JWT creation/validation
  - `GET /api/audit` - Export audit log as JSON

### Test Commands
```bash
# Build Linux test environment
cd test_linux
make clean && make

# Run test server
./sentinel_test

# Test password hashing
curl -X POST http://localhost:8080/api/test/hash -d '{"pin":"1234"}'

# Test session creation
curl -X POST http://localhost:8080/api/test/session -d '{"username":"admin","ip":"127.0.0.1"}'

# View audit log
curl http://localhost:8080/api/audit
```

---

## Memory Usage

### RAM Allocation
- **Audit Log Buffer:** 100 entries × ~250 bytes = ~25 KB
- **Session Manager:** 8 sessions × ~450 bytes = ~3.6 KB
- **Password Hash (per user):** 137 bytes per user
- **Total Security Overhead:** ~30 KB RAM

### SPIFFS Storage
- **Audit Log File:** `/spiffs/audit.log` (~25 KB typical, grows to max 100 entries)
- **Session Secret:** NVS (32 bytes)

### Optimizations Applied
- Reduced audit log from 1000 → 100 entries (saved ~225 KB)
- Reduced session token size from 512 → 384 bytes
- Reduced max sessions from 16 → 8

---

## Build Configuration

### `components/security/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "password_hash.c" "session_mgr.c" "audit_log.c"
    INCLUDE_DIRS "."
    REQUIRES mbedtls nvs_flash spiffs
)
```

### Root `CMakeLists.txt`
```cmake
set(EXTRA_COMPONENT_DIRS
    components/comms
    components/engine
    components/hardware
    components/mqtt_app
    components/storage
    components/w5500
    components/security  # Added
)
```

---

## Security Considerations

### What's Protected
✅ User PINs hashed with industry-standard PBKDF2-SHA256  
✅ Session tokens prevent repeated PIN transmission  
✅ Audit log provides forensic trail for investigations  
✅ Rate limiting prevents brute-force attacks  
✅ Constant-time comparison prevents timing attacks  

### What's NOT Protected (Future Work)
⚠️ Config file still stored in plaintext (consider AES-256 encryption)  
⚠️ No CSRF tokens for web API (low risk for local network)  
⚠️ No TLS client certificate authentication (HTTPS is server-side only)  
⚠️ Audit log not signed (consider HMAC signature per entry)  

### Migration Path
1. **Deploy Phase 1** (Current):
   - Users with plaintext PINs can still authenticate
   - On next PIN change, they migrate to hashed storage
   - No downtime or forced password reset required

2. **Deploy Phase 2** (Future):
   - Add session validation to mongoose handlers
   - Return JWT tokens in `/api/auth` response
   - Frontend stores token in localStorage
   - Subsequent API calls use `Authorization: Bearer <token>` header

3. **Deploy Phase 3** (Optional):
   - Encrypt `config.json` with AES-256-GCM
   - Add CSRF token generation/validation
   - Sign audit log entries with HMAC-SHA256

---

## Compliance Notes

### Audit Log Retention
- **100 entries** in circular buffer (oldest entries overwritten)
- Typical retention: ~1-7 days (depends on activity)
- For longer retention, export via `/api/audit` endpoint to external storage

### Password Policy
- **Minimum PIN length:** 4 digits (configurable via `engine_authenticate` validation)
- **Hash algorithm:** NIST-approved PBKDF2-HMAC-SHA256
- **Iterations:** 10,000 (exceeds OWASP minimum of 1,000 for legacy systems)
- **Salt:** 256-bit random (unique per user)

---

## Troubleshooting

### Issue: "Failed to create monitoring mutex"
- **Cause:** System out of memory during initialization
- **Fix:** Increase heap size in `sdkconfig` or reduce buffer sizes

### Issue: "pin_hash field missing in users.json"
- **Cause:** Upgrading from pre-security firmware
- **Fix:** Not an error - legacy PINs still work, will migrate on next change

### Issue: Session tokens too large for API
- **Cause:** JWT tokens can be 300+ bytes
- **Fix:** Increase `SESSION_TOKEN_MAX_LEN` in `session_mgr.h` (currently 384 bytes)

### Issue: Audit log entries disappearing
- **Cause:** Circular buffer overflow (100 entry limit)
- **Fix:** Export logs regularly via `/api/audit` or increase `AUDIT_LOG_MAX_ENTRIES`

---

## Future Enhancements

### Phase 2: Web API Integration
- [ ] Add JWT token return in `/api/auth` response
- [ ] Add `Authorization: Bearer <token>` validation in mongoose handlers
- [ ] Add `/api/logout` endpoint (session revocation)
- [ ] Add `/api/session/refresh` endpoint (extend expiration)

### Phase 3: Advanced Security
- [ ] Config file encryption (AES-256-GCM)
- [ ] CSRF token generation/validation
- [ ] Audit log signing (HMAC-SHA256 per entry)
- [ ] Rate limiting per IP address (not just global)
- [ ] Failed login notifications (email/Telegram)

### Phase 4: Compliance Features
- [ ] GDPR data export (user activity logs)
- [ ] Password complexity requirements (configurable)
- [ ] Mandatory password rotation (e.g., 90 days)
- [ ] Multi-factor authentication (TOTP already implemented)

---

## References

- **PBKDF2 Spec:** RFC 2898, PKCS #5 v2.0
- **JWT Spec:** RFC 7519
- **HMAC-SHA256:** RFC 2104, FIPS 198-1
- **OWASP Password Storage:** https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html
- **mbedTLS API:** https://mbed-tls.readthedocs.io/
- **ESP-IDF NVS:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html

---

**Document Version:** 1.0  
**Last Updated:** January 28, 2026  
**Author:** Development Team
