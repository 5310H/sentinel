#ifndef OTP_H
#define OTP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate a TOTP code from a base32-encoded secret
 * 
 * @param secret_base32 Base32-encoded secret key (e.g., "JBSWY3DPEHPK3PXP")
 * @param time_step Time step in seconds (default 30)
 * @param digits Number of digits in code (default 6)
 * @return Generated TOTP code (0-999999 for 6 digits), or 0 on error
 */
uint32_t get_totp_code(const char* secret_base32, uint32_t time_step, uint8_t digits);

/**
 * @brief Validate a TOTP code against a base32-encoded secret
 * 
 * @param secret_base32 Base32-encoded secret key
 * @param code 6-digit code string to validate (e.g., "123456")
 * @param time_step Time step in seconds (default 30)
 * @param digits Number of digits expected (default 6)
 * @param clock_skew_tolerance Number of time windows to check on each side (±1 = check 3 windows)
 * @return true if code is valid, false otherwise
 */
bool validate_totp(const char* secret_base32, const char* code, 
                    uint32_t time_step, uint8_t digits, int clock_skew_tolerance);

/**
 * @brief Decode a base32-encoded string to binary
 * 
 * @param encoded Base32-encoded string
 * @param result Output buffer for decoded bytes
 * @param buf_len Size of output buffer
 * @return Number of bytes decoded, or -1 on error
 */
int base32_decode(const char* encoded, uint8_t* result, int buf_len);

#ifdef __cplusplus
}
#endif

#endif // OTP_H