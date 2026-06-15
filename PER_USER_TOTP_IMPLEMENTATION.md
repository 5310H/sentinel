# Per-User TOTP Secrets Implementation

## Summary

Implemented per-user TOTP secret storage and management, replacing the hardcoded default secret with individual secrets for each user.

## Changes Made

### 1. User Structure Updated (`components/storage/storage_mgr.h`)
- Added `totp_secret[STR_MEDIUM]` field to `user_t` struct
- Stores base32-encoded TOTP secrets per user
- Empty string means user doesn't have 2FA configured

### 2. Storage Persistence (`components/storage/storage_mgr.c`)
- **`storage_save_users()`**: Now saves `requires_2fa` and `totp_secret` to JSON
- **`storage_load_all()`**: Loads `requires_2fa` and `totp_secret` from JSON
- Backward compatible - existing users.json files without TOTP secrets will work

### 3. User Management (`components/engine/user_mgr.c`)
- Added `user_set_totp_secret()` function to set/update/clear user TOTP secrets
- Automatically sets `requires_2fa = true` when secret is set
- Clears `requires_2fa` when secret is removed

### 4. Authentication Engine (`components/engine/engine.c`)
- Added `engine_get_user_totp_secret()` function
- Retrieves user-specific TOTP secret by username
- Returns NULL if user not found or secret not set

### 5. Backend API Updates

#### Authentication Endpoint (`/api/auth`)
- **Step 1 (PIN)**: Returns `requires_totp: true/false` instead of sending secret
- **Step 2 (TOTP)**: Uses per-user secret for validation
- Falls back to default secret only if user has no secret configured (for backward compatibility)

#### New Endpoint (`/api/users/set-totp`)
- **POST** `/api/users/set-totp`
- **Body**: `{"name": "username", "totp_secret": "JBSWY3DPEHPK3PXP"}`
- Sets or updates user's TOTP secret
- Empty/null `totp_secret` clears 2FA for user
- Returns `{"status": "ok"}` on success

### 6. Frontend Updates (`test_linux/index.html`)
- Removed client-side TOTP validation (secrets no longer sent to client)
- Removed hardcoded `DEFAULT_TOTP_SECRET`
- All validation now happens server-side (more secure)
- Handles case where user doesn't have 2FA configured

## Security Improvements

✅ **Secrets Never Sent to Client**: TOTP secrets stay on server only
✅ **Per-User Secrets**: Each user has their own secret
✅ **Server-Side Validation**: All TOTP validation happens on backend
✅ **Backward Compatible**: Existing users without secrets still work

## Usage Examples

### Setting a User's TOTP Secret

```bash
# Set TOTP secret for user "Alice"
curl -X POST http://localhost/api/users/set-totp \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice", "totp_secret": "JBSWY3DPEHPK3PXP"}'
```

### Clearing a User's TOTP Secret

```bash
# Remove 2FA for user "Alice"
curl -X POST http://localhost/api/users/set-totp \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice", "totp_secret": ""}'
```

### Authentication Flow

1. **User enters PIN** → `/api/auth` with `step: "pin"`
2. **Server responds** with `requires_totp: true/false`
3. **If required**: User enters TOTP code
4. **User submits TOTP** → `/api/auth` with `step: "totp"` + PIN + TOTP
5. **Server validates** using user's stored secret
6. **Success**: User authenticated

## Storage Format

### users.json Example

```json
[
  {
    "name": "Alice",
    "pin": "1234",
    "email": "alice@example.com",
    "is_admin": true,
    "requires_2fa": true,
    "totp_secret": "JBSWY3DPEHPK3PXP"
  },
  {
    "name": "Bob",
    "pin": "5678",
    "email": "bob@example.com",
    "is_admin": false,
    "requires_2fa": false
  }
]
```

## Migration Notes

### Existing Users
- Users without `totp_secret` in JSON will have empty secret
- Authentication will fall back to default secret (for backward compatibility)
- To enable 2FA: Use `/api/users/set-totp` endpoint

### Default Secret Behavior
- If user has no secret configured, system falls back to default `"JBSWY3DPEHPK3PXP"`
- This allows existing installations to continue working
- **Recommendation**: Set per-user secrets for all users in production

## Next Steps (Optional Enhancements)

1. **QR Code Generation**: Add endpoint to generate QR codes for authenticator apps
2. **Secret Generation**: Add endpoint to generate random secrets server-side
3. **Backup Codes**: Generate and store backup codes during 2FA setup
4. **Admin UI**: Add UI in users.html to manage TOTP secrets
5. **Enforcement**: Make 2FA mandatory for admin users

## Testing

### Test Per-User Secret

1. Set secret for user:
   ```bash
   curl -X POST http://localhost/api/users/set-totp \
     -H "Content-Type: application/json" \
     -d '{"name": "TestUser", "totp_secret": "JBSWY3DPEHPK3PXP"}'
   ```

2. Add secret to Google Authenticator (or similar app)

3. Login with PIN + TOTP code from app

4. Should authenticate successfully

### Test Different Users

- User A with secret "ABC123..."
- User B with secret "XYZ789..."
- Each should only validate with their own secret

## Files Modified

- `components/storage/storage_mgr.h` - Added `totp_secret` field
- `components/storage/storage_mgr.c` - Save/load TOTP secrets
- `components/engine/engine.h` - Added `engine_get_user_totp_secret()`
- `components/engine/engine.c` - Implemented secret retrieval
- `components/engine/user_mgr.h` - Added `user_set_totp_secret()`
- `components/engine/user_mgr.c` - Implemented secret management
- `main/main.c` - Updated auth endpoint, added set-totp endpoint
- `test_linux/main_mock.c` - Same updates for Linux mock
- `test_linux/index.html` - Removed client-side validation

---

**Status**: ✅ Complete - Per-user TOTP secrets fully implemented!
