#ifndef PASSWORD_HASH_H
#define PASSWORD_HASH_H

/**
 * @file password_hash.h
 * @brief Secure password hashing using SHA-256 + salt
 * 
 * Provides bcrypt-style password hashing for user authentication.
 * Uses mbedTLS SHA-256 with per-user random salt.
 * 
 * Security Features:
 * - 32-byte random salt per password
 * - SHA-256 hashing (256-bit output)
 * - Constant-time comparison to prevent timing attacks
 * - Configurable iteration count for future-proofing
 * 
 * Note: ESP32 hardware acceleration available for SHA-256
 */

#include <stdint.h>
#include <stdbool.h>

#define PASSWORD_HASH_SALT_LEN 32     // 256-bit salt
#define PASSWORD_HASH_OUTPUT_LEN 32   // SHA-256 = 256 bits = 32 bytes
#define PASSWORD_HASH_ITERATIONS 10000 // PBKDF2 iterations

/**
 * @brief Password hash storage format
 * 
 * Stored in user_t struct. Contains both salt and hash
 * to support verification without storing plaintext.
 */
typedef struct {
    uint8_t salt[PASSWORD_HASH_SALT_LEN];       // Random salt (32 bytes)
    uint8_t hash[PASSWORD_HASH_OUTPUT_LEN];     // SHA-256 output (32 bytes)
    uint32_t iterations;                         // PBKDF2 iteration count
} password_hash_t;

/**
 * @brief Create password hash from plaintext PIN
 * 
 * Generates random salt and computes PBKDF2-SHA256 hash.
 * Uses ESP32 hardware RNG for salt generation.
 * 
 * @param pin Plaintext PIN (null-terminated, typically 4-8 digits)
 * @param out_hash Output structure to store salt + hash
 * @return true on success, false on error
 * 
 * @example
 * password_hash_t hash;
 * if (password_hash_create("1234", &hash)) {
 *     // Store hash in user database
 *     memcpy(&user.pin_hash, &hash, sizeof(password_hash_t));
 * }
 */
bool password_hash_create(const char *pin, password_hash_t *out_hash);

/**
 * @brief Verify PIN against stored hash
 * 
 * Computes hash of input PIN using stored salt and compares
 * with stored hash using constant-time comparison.
 * 
 * @param pin Plaintext PIN to verify
 * @param stored_hash Previously created hash from password_hash_create()
 * @return true if PIN matches, false otherwise
 * 
 * @example
 * if (password_hash_verify("1234", &user.pin_hash)) {
 *     // Authentication successful
 * }
 */
bool password_hash_verify(const char *pin, const password_hash_t *stored_hash);

/**
 * @brief Convert hash to hex string for storage/debugging
 * 
 * @param hash Hash structure to convert
 * @param out_hex Output buffer (must be at least 129 bytes: 64 chars + null)
 * @return Pointer to out_hex
 */
char *password_hash_to_hex(const password_hash_t *hash, char *out_hex);

/**
 * @brief Parse hash from hex string
 * 
 * @param hex_str Hex string from password_hash_to_hex()
 * @param out_hash Output hash structure
 * @return true on success, false on parse error
 */
bool password_hash_from_hex(const char *hex_str, password_hash_t *out_hash);

#endif // PASSWORD_HASH_H
