# 2FA Fix Implementation - Optional 2FA with Default Fallback

**Date**: January 28, 2026  
**Status**: ✅ COMPLETED  
**Severity**: HIGH (Security Issue)

## What Was Fixed

### Problem
The 2FA implementation had a critical security flaw: users without a configured TOTP secret silently fell back to using a hardcoded default secret (`JBSWY3DPEHPK3PXP`). This allowed:
- Users to "forget" to enable 2FA while still having authentication work
- Anyone knowing the default secret to bypass 2FA for unconfigured users
- No enforcement of the `requires_2fa` flag

### Solution
Modified authentication logic to check the `requires_2fa` flag **before** allowing fallback:

1. **If user has no TOTP secret AND `requires_2fa=true`**: Authentication fails with error "2FA is required but not configured"
2. **If user has no TOTP secret AND `requires_2fa=false`**: PIN-only authentication succeeds
3. **If user has TOTP secret**: Validate TOTP code (previous behavior preserved)

## Files Modified

### 1. [main/main.c](main/main.c) - ESP32 Main Firmware
**Lines**: 193-230  
**Changes**:
- Removed hardcoded fallback to "JBSWY3DPEHPK3PXP"
- Added check for `res.requires_2fa` flag
- If 2FA required but not configured: return error
- If 2FA not required and not configured: return success (PIN-only)
- If 2FA configured: validate TOTP code

**Before**:
```c
if (!user_secret || strlen(user_secret) == 0) {
    user_secret = "JBSWY3DPEHPK3PXP";  // Hardcoded fallback
}
```

**After**:
```c
if (!user_secret || strlen(user_secret) == 0) {
    if (res.requires_2fa) {
        // 2FA required but not configured - fail
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
            "{\"authenticated\":false,\"error\":\"2FA is required but not configured\"}");
    } else {
        // 2FA not required - allow PIN-only
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
            "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d}",
            res.is_admin ? "true" : "false", res.name, engine_get_arm_state());
    }
}
```

### 2. [test_linux/main_mock.c](test_linux/main_mock.c) - Linux Mock for Testing
**Lines**: 272-309  
**Changes**: Identical to main.c (keeps implementations in sync)

## Behavior Changes

### Authentication Flow - No TOTP Secret Configured

| User Type | Before | After |
|-----------|--------|-------|
| `requires_2fa=false` | PIN validates ✓ | PIN validates ✓ (unchanged) |
| `requires_2fa=true` | PIN validates, falls back to default secret | AUTH FAILS ✗ (fixed) |

### Authentication Flow - TOTP Secret Configured

| User Type | Before | After |
|-----------|--------|-------|
| `requires_2fa=false` | TOTP validated | TOTP validated (unchanged) |
| `requires_2fa=true` | TOTP validated | TOTP validated (unchanged) |

## Testing

### Test 1: User Without 2FA Secret (requires_2fa=true)

```bash
# Attempt authentication
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","step":"pin"}'

# Expected response (NEW BEHAVIOR):
# {"authenticated":false,"error":"2FA is required but not configured"}
```

### Test 2: User Without 2FA Secret (requires_2fa=false)

```bash
# Attempt authentication
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","step":"pin"}'

# Expected response (PRESERVED):
# {"authenticated":true,"is_admin":false,"name":"user","state":1}
```

### Test 3: User With 2FA Secret Configured

```bash
# First get the current TOTP code
python3 -c "import pyotp; print(pyotp.TOTP('JBSWY3DPEHPK3PXP').now())"

# Attempt authentication with valid TOTP
curl -X POST http://localhost:8000/api/auth \
  -H "Content-Type: application/json" \
  -d '{"pin":"1234","totp":"123456","step":"totp"}'

# Expected response:
# {"authenticated":true,"is_admin":false,"name":"user","state":1}
```

## Security Impact

### Before Fix (Vulnerable)
```
User "admin" with requires_2fa=true but no secret configured
  ↓
Authentication still succeeds using hardcoded "JBSWY3DPEHPK3PXP"
  ↓
Anyone knowing this secret can authenticate as admin
  ↓
2FA requirement is bypassed
```

### After Fix (Secure)
```
User "admin" with requires_2fa=true but no secret configured
  ↓
Authentication fails with error "2FA is required but not configured"
  ↓
User must first configure their personal TOTP secret
  ↓
2FA requirement is enforced
```

## Implementation Details

### API Response Changes

**When 2FA required but not configured**:
```json
{
  "authenticated": false,
  "error": "2FA is required but not configured"
}
```

**When 2FA not required and not configured**:
```json
{
  "authenticated": true,
  "is_admin": false,
  "name": "username",
  "state": 1
}
```

### Code Logic Flow

```
1. User submits PIN
   ↓
2. Check PIN validity (engine_check_keypad)
   ↓
3. Get user's configured TOTP secret
   ↓
4. If no secret:
      ├→ If requires_2fa=true: FAIL (new behavior)
      └→ If requires_2fa=false: SUCCESS with PIN-only
   ↓
5. If secret configured:
      └→ Validate TOTP code with ±1 time window
         ├→ Valid: SUCCESS
         └→ Invalid: FAIL
```

## Frontend Compatibility

The frontend UI should handle the new error response:

```javascript
async function submitPin() {
    // ... existing PIN validation ...
    
    if (d.authenticated) {
        // Success - show authenticated UI or 2FA step
        validatedPin = currentPin;
    } else if (d.error === "2FA is required but not configured") {
        // NEW: Show error message to user
        alert("2FA is required for your account. Please configure it in settings.");
        // Don't show TOTP input
    } else {
        // Other errors (invalid PIN)
        alert(d.error || "Authentication failed");
    }
}
```

## Related Documentation

- See [2FA_QUICK_FIXES.md](2FA_QUICK_FIXES.md) for full list of 2FA issues and fixes
- See [2FA_SECURITY_REVIEW.md](2FA_SECURITY_REVIEW.md) for comprehensive security audit
- See [TOTP_IMPLEMENTATION.md](TOTP_IMPLEMENTATION.md) for TOTP algorithm details

## Next Steps

### Remaining High-Priority Fixes
1. ⚠️ **Session Management** - Add session tokens and timeouts (MEDIUM priority)
2. ⚠️ **Audit Logging** - Log all authentication attempts (MEDIUM priority)
3. ⚠️ **Rate Limiting** - Prevent brute force on TOTP codes (MEDIUM priority)

### Recommended Enhancements
1. 🔧 Add QR code generation for TOTP setup
2. 🔧 Implement backup codes for account recovery
3. 🔧 Add TOTP setup/enrollment endpoint
4. 🔧 Frontend UI for user to set `requires_2fa` flag

## Validation

✅ Code changes compiled successfully  
✅ Both main.c and main_mock.c updated consistently  
✅ `res.requires_2fa` field already exists in user_t struct  
✅ Error messages clear and actionable  
✅ Backward compatible with users who have TOTP secrets configured  

## Summary

This fix enforces the `requires_2fa` flag by preventing silent fallback to a hardcoded default secret. Users with `requires_2fa=true` must now configure their personal TOTP secret before authentication succeeds, significantly improving security posture.
