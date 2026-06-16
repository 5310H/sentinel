#ifndef USER_MGR_H
#define USER_MGR_H

/**
 * @file user_mgr.h
 * @brief System user management (add/update/delete/list)
 * 
 * Manages authenticated user database for:
 * - Keypad entry (PIN-based arm/disarm)
 * - Notification preferences (email, SMS, Telegram)
 * - Admin privileges (can add/remove users, change config)
 * - Usage logging and access history
 * 
 * Features:
 * - Dynamic user array with max 16 users
 * - JSON serialization for persistence (users.json)
 * - PIN validation with rate limiting
 * - Notification preference per user
 * - Admin/user role separation
 * 
 * Note: User array always includes index 0 = master (admin) user
 */

#include <stdbool.h>
#include "storage_mgr.h"

/**
 * @brief Serialize all users to JSON string
 * 
 * Converts user_t array to JSON format for storage.
 * Used by storage_save_users() to persist user database.
 * 
 * @return Pointer to JSON string buffer (statically allocated)
 * 
 * @example
 * const char *json = users_to_json();
 * storage_save_users(json);  // Persist to SPIFFS
 */
char *users_to_json(void);

/**
 * @brief Add new system user
 * 
 * Creates user entry with PIN, contact info, and notification preferences.
 * Validates inputs before adding (PIN format, email format, etc.).
 * Updates user count and persists to storage.
 * 
 * @param users User array (assumes MAX_USERS size)
 * @param storage_get_user_count() Pointer to user count (incremented on success)
 * @param name Username (max 32 chars, e.g., "John Smith")
 * @param pin Numeric PIN (4-8 digits recommended)
 * @param phone Phone number for SMS (optional, can be "")
 * @param email Email address for notifications
 * @param notify Notification preference (1=None, 2=Email, 3=Telegram, 4=Noonlight)
 * @param is_admin true for admin privileges, false for regular user
 * 
 * @example
 * user_add(users, &user_count, "Alice", "1234", "555-1234", "alice@example.com", 2, true);
 * storage_save_users(users_to_json());
 */
void user_add(const char *name, const char *pin, 
              const char *phone, const char *email, int notify, bool is_admin, const char *emergency_pin);

/**
 * @brief Set or update a user's TOTP secret
 * 
 * @param users User array
 * @param count User count
 * @param name Username to update
 * @param totp_secret Base32-encoded TOTP secret (or NULL/empty to clear)
 * @return true if user found and updated, false otherwise
 */
bool user_set_totp_secret(const char *name, const char *totp_secret);


/**
 * @brief Update existing user information
 * 
 * Modifies user by name with new PIN, contact info, and permissions.
 * Validates changes and persists to storage.
 * Master user (index 0) can only be updated by itself.
 * 
 * @param users User array
 * @param storage_get_user_count() User count
 * @param name Username to find and update
 * @param new_pin New PIN (optional, "" to keep current)
 * @param new_phone New phone number (optional)
 * @param new_email New email address (optional)
 * @param new_notify New notification preference
 * @param new_is_admin New admin flag
 * 
 * @example
 * user_update(users, user_count, "Alice", "", "555-5678", "newemail@example.com", 2, false);
 * storage_save_users(users_to_json());
 */
void user_update(const char *name, const char *new_pin, 
                 const char *new_phone, const char *new_email, int new_notify, int new_is_admin, const char *new_emergency_pin);

/**
 * @brief Delete user from system
 * 
 * Removes user from array by name.
 * Decrements user count and persists changes.
 * Cannot delete master user (index 0).
 * 
 * @param users User array
 * @param storage_get_user_count() Pointer to user count (decremented on success)
 * @param name Username to delete
 * 
 * @example
 * user_drop(users, &user_count, "Alice");
 * storage_save_users(users_to_json());
 */
void user_drop(const char *name);

/**
 * @brief Display all users to console/log
 * 
 * Prints formatted table of all configured users with:
 * - Index number
 * - Name
 * - Phone
 * - Email
 * - Notification preference
 * - Admin flag
 * 
 * Useful for debugging and verification.
 * 
 * @param users User array
 * @param storage_get_user_count() Number of users
 * 
 * @example
 * user_list(users, user_count);
 * // Output:
 * // Index | Name      | Phone    | Email           | Notify | Admin
 * // 0     | Master    | 555-0000 | admin@home.com  | 2      | Y
 * // 1     | Alice     | 555-1234 | alice@home.com  | 2      | N
 */
void user_list(void);

#endif