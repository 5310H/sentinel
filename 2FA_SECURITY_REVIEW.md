# 2FA Authentication Review

## Current Implementation Status

### ✅ What's Working

**1. TOTP Validation Algorithm**
- RFC 6238-compliant TOTP (Time-based One-Time Password)
- Proper HMAC-SHA1 implementation with mongoose library
- Base32 decoding for secret keys
- Clock skew tolerance: ±1 time window (validates codes from 30 seconds before/after)
- 6-digit code validation with input sanitization

**2. Two-Step Authentication Flow**
- Step 1: PIN validation with optional 2FA requirement check
- Step 2: TOTP code validation if 2FA is required
- Backward compatibility: Legacy PIN-only mode still supported

**3. User TOTP Secret Management**
- Per-user TOTP secrets stored in user_t structure
- Secrets are base32-encoded (RFC 4648 compliant)
- `user_set_totp_secret()` to set/update user secrets
- `user_get_totp_secret()` to retrieve user's secret
- Secrets persisted to storage (users.json)

**4. API Endpoints**
- `/api/auth` with step parameter:
  - `step=pin`: Validate PIN, indicate if 2FA is required
  - `step=totp`: Validate TOTP code after PIN validation
- `/api/users/set-totp`: Set/update user TOTP secret
- Both ESP32 main and Linux mock implementations

---

## ⚠️ Issues Identified

### Critical Issues

**1. Linux Mock Still Uses Default Secret**
- **Location**: `test_linux/main_mock.c` lines 277-278
- **Problem**: Hardcoded default secret instead of per-user secrets
- **Current Code**:
  ```c
  const char* user_secret = "JBSWY3DPEHPK3PXP";  // Default secret
  ```
- **Impact**: All users on Linux mock share the same 2FA secret
- **Fix**: Should call `engine_get_user_totp_secret(res.name)` like main.c does

**2. Missing TOTP Secret for Master User (ESP32)**
- **Location**: `main/main.c` lines 199-207
- **Problem**: When no user secret is found, uses hardcoded default fallback
- **Current Code**:
  ```c
  if (!user_secret || strlen(user_secret) == 0) {
      user_secret = "JBSWY3DPEHPK3PXP";  // Default fallback
  }
  ```
- **Impact**: Inconsistent security - some users don't have unique secrets
- **Fix**: Should require 2FA setup or reject if no secret configured

**3. No Session Management**
- **Problem**: Authentication response indicates success/failure but no session token
- **Impact**: Multiple authenticators could be used for same user
- **Risk**: Weak multi-user isolation

---

### Design Issues

**1. 2FA is Optional**
- **Current Behavior**: 2FA only enabled if user has TOTP secret configured
- **Design Problem**: 
  - Users without configured secrets silently fall back to PIN-only
  - Master user never enforces 2FA
  - No admin control over 2FA requirement
- **Recommendation**: 
  - Add `requires_2fa` flag to user_t (already exists but not enforced)
  - Add enforcement logic during authentication

**2. No QR Code for Setup**
- **Problem**: Users must manually enter base32 secrets into authenticator apps
- **Impact**: Difficult user experience, error-prone setup
- **Recommendation**: Add QR code generation endpoint

**3. Client Never Receives Secret**
- **Current Code** (main.c line 174):
  ```c
  // Don't send secret to client - security risk
  ```
- **Design Issue**: No way for user to verify their secret matches the server
- **Recommendation**: Send secret at setup time, but not after authentication

**4. No Logout or Session Invalidation**
- **Problem**: No way to invalidate a session
- **Impact**: Once authenticated, no timeout or way to force logout
- **Recommendation**: Add session tokens with expiration

---

## Detailed Code Analysis

### Authentication Flow (ESP32 - main.c)

```
User Request: POST /api/auth
├── Body: {"pin": "1234", "step": "pin"}
│
├─→ Step 1: PIN Validation
│   ├─ engine_check_keypad(pin) → validates PIN
│   ├─ If PIN valid:
│   │  ├─ Check user's TOTP secret exists
│   │  ├─ If 2FA configured: return requires_totp=true
│   │  └─ If no 2FA: return authenticated=true (SECURITY ISSUE)
│   └─ If PIN invalid: return authenticated=false
│
└─→ Step 2: TOTP Validation
    ├── Body: {"pin": "1234", "totp": "123456", "step": "totp"}
    ├─ Verify PIN is still valid
    ├─ Get user's TOTP secret
    ├─ If no secret exists:
    │  └─ Use hardcoded default (SECURITY ISSUE)
    ├─ validate_totp(secret, code, 30, 6, 1)
    └─ Return success/failure
```

### Issues in Flow

| Issue | Line | Severity | Impact |
|-------|------|----------|--------|
| PIN without 2FA returns success | 185 | HIGH | 2FA can be bypassed by not setting secret |
| Default fallback secret | 202 | HIGH | Weak security for unconfigured users |
| No session tokens | N/A | MEDIUM | No secure session management |
| No logout endpoint | N/A | MEDIUM | Sessions never expire |
| Client can't verify secret | 174 | LOW | Poor UX for 2FA setup |

---

## Linux Mock Issues

### Issue 1: Hardcoded Default Secret
- **File**: `test_linux/main_mock.c` line 277
- **Current**:
  ```c
  const char* user_secret = "JBSWY3DPEHPK3PXP";  // Default secret
  ```
- **Should Be**:
  ```c
  const char* user_secret = engine_get_user_totp_secret(res.name);
  if (!user_secret || strlen(user_secret) == 0) {
      // Handle missing secret: reject or use fallback
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
          "{\"authenticated\":false,\"error\":\"2FA not configured\"}");
      return;
  }
  ```

### Issue 2: Missing Fallback Behavior
- **File**: `test_linux/main_mock.c` lines 276-283
- **Current**: Silently uses default secret
- **Should Be**: Either:
  - Option A: Require 2FA setup (stricter)
  - Option B: Allow PIN-only if no secret (current behavior, but explicit)

---

## Recommendations

### Priority 1 (Critical)

**1. Fix Linux Mock 2FA Secret Loading**
```c
// Replace lines 277-278 in test_linux/main_mock.c
const char* user_secret = engine_get_user_totp_secret(res.name);

// Add proper fallback handling:
if (!user_secret || strlen(user_secret) == 0) {
    // User hasn't set up 2FA - either reject or allow PIN-only
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"authenticated\":false,\"error\":\"2FA not configured\"}");
    return;
}
```

**2. Enforce 2FA When `requires_2fa=true`**
```c
// In both main.c and main_mock.c, check the requires_2fa flag
bool has_2fa = (user_secret != NULL && strlen(user_secret) > 0);
bool user_requires_2fa = false;

// Get user's requires_2fa setting (need to expose from user_mgr)
for (int i = 0; i < u_count; i++) {
    if (strcmp(users[i].name, res.name) == 0) {
        user_requires_2fa = users[i].requires_2fa;
        break;
    }
}

// If 2FA required but not configured, fail
if (user_requires_2fa && !has_2fa) {
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"authenticated\":false,\"error\":\"2FA required but not configured\"}");
    return;
}
```

**3. Remove Hardcoded Fallback Secrets**
- Remove the default `"JBSWY3DPEHPK3PXP"` fallback
- Force users to configure 2FA if required
- Make behavior explicit (no silent fallbacks)

### Priority 2 (Important)

**4. Add Session Management**
```c
// Create session token on successful auth
const char* session_id = generate_session_token();
store_session(session_id, username, time(NULL) + 3600);

// Return session in response
mg_http_reply(c, 200, "Content-Type: application/json\r\n",
    "{\"authenticated\":true,\"session\":\"%s\",\"expires\":%ld}",
    session_id, time(NULL) + 3600);
```

**5. Add Logout Endpoint**
```c
// POST /api/logout
else if (mg_strcmp(hm->uri, mg_str("/api/logout")) == 0) {
    char session_id[256] = {0};
    get_json_str(hm->body, "$.session", session_id, sizeof(session_id));
    
    invalidate_session(session_id);
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"status\":\"logged_out\"}");
}
```

**6. Add 2FA Setup Endpoint**
```c
// POST /api/2fa/setup - Generate QR code and secret
// POST /api/2fa/verify - Confirm 2FA with TOTP code
// POST /api/2fa/disable - Disable 2FA for user (needs current code)
```

### Priority 3 (Nice to Have)

**7. QR Code Generation**
- Generate QR codes at setup time
- Use standard format: `otpauth://totp/username@host?secret=...`
- Include in `/api/2fa/setup` response

**8. Rate Limiting**
- Limit 2FA attempts to prevent brute force
- Lock account after N failed attempts
- Implement backoff (delay after failures)

**9. Audit Logging**
- Log all authentication attempts (success and failure)
- Log 2FA setup/changes
- Include IP address, timestamp, user

---

## Testing Checklist

- [ ] Test PIN + 2FA enabled → requires TOTP code
- [ ] Test PIN + 2FA disabled → allows login without TOTP
- [ ] Test PIN + 2FA never configured → check behavior (accept/reject)
- [ ] Test with valid TOTP code (current window)
- [ ] Test with valid TOTP code from ±1 window
- [ ] Test with expired TOTP code
- [ ] Test with invalid TOTP code format
- [ ] Test with wrong TOTP code
- [ ] Test Linux mock uses per-user secrets (not default)
- [ ] Test user secret persistence after restart
- [ ] Test QR code generation (if implemented)
- [ ] Test session expiration (if implemented)
- [ ] Test logout endpoint (if implemented)

---

## Security Considerations

**Current Weaknesses:**
1. ❌ No session management - users never "log out"
2. ❌ Optional 2FA - can be bypassed by not configuring
3. ❌ Default fallback secrets - weaker than per-user
4. ❌ No rate limiting - brute force possible
5. ❌ No audit logging - can't trace who accessed what

**Strengths:**
1. ✅ Proper TOTP implementation (RFC 6238)
2. ✅ Clock skew tolerance (handles clock drift)
3. ✅ Input validation (6-digit numeric only)
4. ✅ Per-user secrets (once configured)
5. ✅ Persistent storage (survives restarts)

---

## Summary

**Overall Status**: 🟡 **Partially Secure** (60%)

The TOTP validation algorithm is solid and RFC-compliant, but the authentication flow has design issues:
- 2FA is optional and can be silently bypassed
- No session management
- Linux mock has a critical bug
- No audit trail

The implementation would benefit from:
1. Fixing the Linux mock (HIGH PRIORITY)
2. Enforcing 2FA when configured (HIGH PRIORITY)
3. Adding session tokens (MEDIUM PRIORITY)
4. Removing hardcoded fallbacks (HIGH PRIORITY)
5. Adding audit logging (MEDIUM PRIORITY)
