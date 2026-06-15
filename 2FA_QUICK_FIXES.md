# 2FA Authentication Issues & Fixes

## Status Summary

- ✅ **TOTP Algorithm**: RFC 6238-compliant with clock skew tolerance
- ✅ **ESP32 Implementation**: Proper per-user secrets
- ✅ **Linux Mock**: Fixed to use per-user secrets (was using hardcoded default)
- ⚠️ **Optional 2FA**: Can be bypassed with fallback default secret
- ❌ **Session Management**: Not implemented
- ❌ **Audit Logging**: Not implemented

---

## Recent Fixes

### 1. ✅ Linux Mock - Per-User Secrets (COMPLETED)

**File**: `test_linux/main_mock.c` lines 265-298

**What Changed**:
```diff
- // Get user's TOTP secret (for now using default, should be per-user)
- // TODO: Get actual user-specific secret from user_t struct
- const char* user_secret = "JBSWY3DPEHPK3PXP";  // Default secret

+ // Get user's TOTP secret (per-user, not default)
+ const char* user_secret = engine_get_user_totp_secret(res.name);
+ 
+ // If user has no TOTP secret set, check if 2FA is required
+ if (!user_secret || strlen(user_secret) == 0) {
+     // User doesn't have 2FA configured - check if it's required
+     // For now, if no secret is set, we'll use default secret as fallback
+     // In production, you might want to require 2FA setup
+     user_secret = "JBSWY3DPEHPK3PXP";  // Default fallback
+ }
```

**Impact**: 
- ✅ Linux mock now matches ESP32 behavior
- ✅ Both use per-user secrets from `engine_get_user_totp_secret()`
- ✅ Both have same fallback behavior for unconfigured users

---

## Critical Issues Remaining

### 2. Add TOTP Secret to User Structure

**Add to `components/storage/storage_mgr.h`:**
### 1. ⚠️ Optional 2FA - Default Fallback Secret

**Severity**: HIGH  
**Files**: `main/main.c` (197-207), `test_linux/main_mock.c` (277-283)

**Current Behavior**:
- If user doesn't have TOTP secret configured, silently use hardcoded default
- This bypasses security - users can "forget" to enable 2FA and fall back to default

**Why It's a Problem**:
```
User "admin" doesn't have TOTP configured
  → Code uses: "JBSWY3DPEHPK3PXP"
  → Anyone knowing the default secret can bypass 2FA for admin
```

**Proposed Fix**:
```c
// Get user's TOTP secret
const char* user_secret = engine_get_user_totp_secret(res.name);

// Check if 2FA is required for this user
bool user_requires_2fa = false;
for (int i = 0; i < u_count; i++) {
    if (strcmp(users[i].name, res.name) == 0) {
        user_requires_2fa = users[i].requires_2fa;
        break;
    }
}

// If 2FA required but not configured, fail authentication
if (user_requires_2fa && (!user_secret || strlen(user_secret) == 0)) {
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"authenticated\":false,\"error\":\"2FA is required but not configured\"}");
    return;
}

// If no secret and 2FA not required, allow PIN-only
if (!user_secret || strlen(user_secret) == 0) {
    // 2FA not configured and not required - allow PIN-only
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d}",
        res.is_admin ? "true" : "false", res.name, engine_get_arm_state());
    return;
}

// 2FA is configured - validate TOTP
if (strlen(totp_in) == 6) {
    totp_valid = validate_totp(user_secret, totp_in, 30, 6, 1);
}
```

**Effort**: ~30 minutes

---

### 2. ❌ No Session Management

**Severity**: MEDIUM  
**Impact**: Users never "log out", no timeout protection

**What's Needed**:
1. Generate session token on successful authentication
2. Store session with expiration time
3. Validate session token on each request
4. Add `/api/logout` endpoint
5. Implement session timeout (e.g., 1 hour)

**Estimated Effort**: 3-4 hours

---

### 3. ❌ No Audit Logging

**Severity**: MEDIUM  
**Impact**: Can't trace authentication attempts or changes

**What's Needed**:
1. Log all authentication attempts (success/failure)
2. Log 2FA setup/changes
3. Log failed TOTP attempts
4. Include: timestamp, user, IP, action, result

**Estimated Effort**: 2-3 hours

---

### 4. ⚠️ No Rate Limiting on TOTP

**Severity**: MEDIUM  
**Impact**: Brute force attack on TOTP codes possible

**What's Needed**:
1. Track failed TOTP attempts per user
2. Lock account after N failed attempts
3. Implement exponential backoff
4. Return remaining attempts in response

**Estimated Effort**: 1-2 hours

---

### 5. ❌ No QR Code Generation

**Severity**: LOW  
**Impact**: Users must manually enter TOTP secrets

**What's Needed**:
1. Generate QR code for `otpauth://totp/user@host?secret=...`
2. Display QR code during 2FA setup
3. Allow scanning with authenticator apps

**Estimated Effort**: 1-2 hours

---

## Testing 2FA After Fix

### Test 1: Linux Mock Per-User Secrets

```bash
# Start mock
cd test_linux
./sentinel_test

# In another terminal:

# 1. Set TOTP secret for user "admin"
curl -X POST http://localhost:8000/api/users/set-totp \
  -H "Content-Type: application/json" \
  -d '{"name":"admin","totp_secret":"JBSWY3DPEHPK3PXP"}'

# 2. Try authentication with PIN only
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","step":"pin"}'
# Expected: requires_totp=true

# 3. Try authentication with PIN + valid TOTP
# Get a valid TOTP code from Authenticator app or:
# python3 -c "import pyotp; print(pyotp.TOTP('JBSWY3DPEHPK3PXP').now())"
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","totp":"123456","step":"totp"}'
# Expected: authenticated=true (with valid code)

# 4. Try authentication with PIN + invalid TOTP
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","totp":"000000","step":"totp"}'
# Expected: authenticated=false, error="Invalid TOTP code"
```

### Test 2: Default Fallback Behavior

```bash
# With user that has NO TOTP configured:
# 1. PIN-only should work (falls back to default)
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","step":"pin"}'
# Expected: requires_totp=false (no secret)

# 2. Default secret should validate
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","totp":"123456","step":"totp"}'
# Expected: Works with default secret (current fallback behavior)
```

---

## Documentation

- See `2FA_SECURITY_REVIEW.md` for detailed security analysis
- See `TOTP_IMPLEMENTATION.md` for TOTP algorithm details
- See `PER_USER_TOTP_IMPLEMENTATION.md` for per-user secret implementation
