whats the# Installer User Implementation

## Overview
Added automatic creation of an "Installer" user during system initialization when no users exist. This allows the installer to access the system using the master PIN from config before any other users are added.

## Changes Made

### 1. Modified File: `components/storage/storage_mgr.c`

#### Added Header Include (Line 6)
```c
#include "password_hash.h"  // For hashing installer PIN
```

#### Added Initialization Logic (Lines 607-640)
After loading users from `users.json`, if no users exist:
```c
// If no users exist, create Installer user with master PIN from config
if (u_count == 0 && config.pin[0] != '\0') {
    printf("[STORAGE] No users found - Creating Installer user with master PIN\n");
    strncpy(users[0].name, "Installer", STR_SMALL - 1);
    users[0].name[STR_SMALL - 1] = '\0';
    
    // Hash the master PIN
    password_hash_t hash;
    if (password_hash_create(config.pin, &hash)) {
        password_hash_to_hex(&hash, users[0].pin_hash);
        printf("[STORAGE] Created hashed PIN for Installer user\n");
    } else {
        // Fallback: store plaintext (shouldn't happen)
        strncpy(users[0].pin, config.pin, STR_SMALL - 1);
        users[0].pin[STR_SMALL - 1] = '\0';
        printf("[STORAGE] WARNING: Failed to hash PIN, using plaintext fallback\n");
    }
    
    strncpy(users[0].phone, "555-0000", STR_SMALL - 1);
    strncpy(users[0].email, config.email, STR_MEDIUM - 1);
    users[0].notify = 0;  // No notifications for installer
    users[0].is_admin = true;  // Installer has admin rights
    users[0].requires_2fa = false;  // Optional 2FA for installer
    users[0].totp_secret[0] = '\0';  // No TOTP secret yet
    users[0].emergency_pin[0] = '\0';  // No emergency PIN for installer
    users[0].pin[0] = '\0';  // Clear legacy plaintext
    
    u_count = 1;
    storage_save_users();  // Persist the new user
    printf("[STORAGE] Installer user created and persisted to users.json\n");
}
```

## How It Works

### Initialization Flow
1. System loads `config.json` (contains master PIN in `config.pin` field, currently "9999")
2. System loads `users.json` (loads existing users)
3. **NEW**: If `users.json` is empty or missing:
   - Creates "Installer" user entry
   - Hashes the master PIN using PBKDF2-SHA256
   - Sets installer as admin with email from config
   - Persists to `users.json`
   - Logs creation with debug output

### Authentication
The existing `engine_check_keypad()` function handles authentication:

1. First checks master PIN (config.pin) against input
2. Then checks all user PINs (both hashed and legacy plaintext)
3. Installer user works exactly like any other user with hashed PIN

### Key Properties of Installer User
- **Name**: "Installer"
- **PIN**: Master PIN from config.json (hashed with PBKDF2-SHA256)
- **Admin Rights**: Yes (can add/delete other users)
- **2FA Required**: No (optional 2FA)
- **Email**: From config.json
- **Phone**: "555-0000"
- **Notification**: None (notify=0)

## Security Implications

✅ **Benefits**:
- Master PIN is immediately usable as a user entry
- Hashed PIN is stored securely (not plaintext)
- Installer has full admin access to create other users
- PIN is now part of standard authentication flow
- Works with existing 2FA system (can be enabled anytime)

⚠️ **Considerations**:
- First user created has master PIN privileges
- After first access, admin should create additional users
- Installer account should be removed or renamed once setup is complete

## Testing

### Test on Linux Mock
```bash
cd /home/kjgerhart/sentinel-esp
./sentinel_test
```
Expected output during initialization:
```
[STORAGE] No users found - Creating Installer user with master PIN
[STORAGE] Created hashed PIN for Installer user
[STORAGE] Installer user created and persisted to users.json
```

Then verify you can login with:
- PIN: 9999 (master PIN from config)
- Username: "Installer" (shown in auth response)

### Verify Persistence
Check that `data/users.json` now contains:
```json
[
  {
    "name": "Installer",
    "pin_hash": "...",  // Hashed PIN
    "phone": "555-0000",
    "email": "5310@trahreg.com",
    "notify": 0,
    "is_admin": true,
    "requires_2fa": false,
    "totp_secret": ""
  }
]
```

## Design Pattern

This is a **zero-config initialization** pattern:
- No manual users.json required on first boot
- Master PIN immediately becomes a system user
- Enables installer to add additional users via API
- Follows the principle: installer needs access before anything else

## Related Files

- `components/storage/storage_mgr.c` - User initialization logic
- `components/engine/engine.c` - PIN validation logic (`engine_check_keypad`)
- `components/engine/user_mgr.h` - User management API
- `data/config.json` - Master PIN configuration
- `data/users.json` - User database (auto-created on first boot)

## Future Improvements

1. Add API endpoint to rename/delete Installer user
2. Add prompt to force password change on first login
3. Audit log entry when Installer user is created
4. Optional: Require 2FA setup for Installer on first use
