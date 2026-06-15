#include "otp.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "mongoose.h"  // For mg_sha1_* functions

#ifdef ESP_PLATFORM
#include "esp_sntp.h"
#endif
// Base32 alphabet (RFC 4648)
//static const char base32_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char base32_chars[] __attribute__((unused)) = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
/**
 * @brief Decode base32-encoded string to binary
 */
int base32_decode(const char* encoded, uint8_t* result, int buf_len) {
    if (!encoded || !result || buf_len < 1) {
        return -1;
    }
    
    int buffer = 0;
    int bits_left = 0;
    int count = 0;
    const char* ptr = encoded;
    
    while (count < buf_len && *ptr) {
        uint8_t ch = *ptr++;
        
        // Skip whitespace and hyphens
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '-') {
            continue;
        }
        
        // Handle commonly mistyped characters
        if (ch == '0') ch = 'O';
        else if (ch == '1') ch = 'L';
        else if (ch == '8') ch = 'B';
        
        // Convert character to 5-bit value
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') {
            value = ch - 'A';
        } else if (ch >= 'a' && ch <= 'z') {
            value = ch - 'a';
        } else if (ch >= '2' && ch <= '7') {
            value = 26 + (ch - '2');
        }
        
        if (value < 0) {
            return -1;  // Invalid character
        }
        
        buffer = (buffer << 5) | value;
        bits_left += 5;
        
        if (bits_left >= 8) {
            result[count++] = (buffer >> (bits_left - 8)) & 0xFF;
            bits_left -= 8;
        }
    }
    
    return count;
}

/**
 * @brief Compute HMAC-SHA1
 */
static void hmac_sha1(const uint8_t* key, size_t key_len, 
                       const uint8_t* data, size_t data_len, 
                       uint8_t* hmac_result) {
    mg_sha1_ctx ctx;
    uint8_t k[64] = {0};
    uint8_t o_pad[64], i_pad[64];
    
    // Prepare key (pad or hash if needed)
    if (key_len <= 64) {
        memcpy(k, key, key_len);
    } else {
        // Hash key if longer than 64 bytes
        mg_sha1_init(&ctx);
        mg_sha1_update(&ctx, key, key_len);
        uint8_t key_hash[20];
        mg_sha1_final(key_hash, &ctx);
        memcpy(k, key_hash, 20);
        key_len = 20;
    }
    
    // Create inner and outer padding
    memset(i_pad, 0x36, 64);
    memset(o_pad, 0x5c, 64);
    
    for (size_t i = 0; i < 64; i++) {
        i_pad[i] ^= k[i];
        o_pad[i] ^= k[i];
    }
    
    // Inner hash: SHA1(i_pad || data)
    mg_sha1_init(&ctx);
    mg_sha1_update(&ctx, i_pad, 64);
    mg_sha1_update(&ctx, data, data_len);
    uint8_t inner_hash[20];
    mg_sha1_final(inner_hash, &ctx);
    
    // Outer hash: SHA1(o_pad || inner_hash)
    mg_sha1_init(&ctx);
    mg_sha1_update(&ctx, o_pad, 64);
    mg_sha1_update(&ctx, inner_hash, 20);
    mg_sha1_final(hmac_result, &ctx);
}

/**
 * @brief Dynamic truncation (RFC 4226)
 */
static uint32_t dynamic_truncate(const uint8_t* hmac) {
    int offset = hmac[19] & 0x0F;
    uint32_t binary = ((hmac[offset] & 0x7F) << 24) |
                      ((hmac[offset + 1] & 0xFF) << 16) |
                      ((hmac[offset + 2] & 0xFF) << 8) |
                      (hmac[offset + 3] & 0xFF);
    return binary;
}

/**
 * @brief Generate TOTP code
 */
uint32_t get_totp_code(const char* secret_base32, uint32_t time_step, uint8_t digits) {
    if (!secret_base32 || strlen(secret_base32) == 0) {
        return 0;
    }
    
    // Decode base32 secret
    uint8_t secret_bytes[32];
    int secret_len = base32_decode(secret_base32, secret_bytes, sizeof(secret_bytes));
    if (secret_len < 1) {
        return 0;
    }
    
    // Calculate time counter
    time_t now = time(NULL);
    uint64_t counter = (uint64_t)(now / time_step);
    
    // Convert counter to big-endian 8-byte array
    uint8_t counter_bytes[8];
    for (int i = 7; i >= 0; i--) {
        counter_bytes[i] = counter & 0xFF;
        counter >>= 8;
    }
    
    // Compute HMAC-SHA1
    uint8_t hmac[20];
    hmac_sha1(secret_bytes, secret_len, counter_bytes, 8, hmac);
    
    // Dynamic truncation
    uint32_t binary = dynamic_truncate(hmac);
    
    // Generate code with specified digits
    uint32_t power = 1;
    for (int i = 0; i < digits; i++) {
        power *= 10;
    }
    
    return binary % power;
}

/**
 * @brief Validate TOTP code with clock skew tolerance
 */
bool validate_totp(const char* secret_base32, const char* code, 
                    uint32_t time_step, uint8_t digits, int clock_skew_tolerance) {
    if (!secret_base32 || !code || strlen(code) != digits) {
        return false;
    }
    
    // Validate code contains only digits
    for (int i = 0; i < digits; i++) {
        if (code[i] < '0' || code[i] > '9') {
            return false;
        }
    }
    
    // Convert code string to number
    uint32_t code_value = 0;
    for (int i = 0; i < digits; i++) {
        code_value = code_value * 10 + (code[i] - '0');
    }
    
    // Check current time window and ±clock_skew_tolerance windows
    time_t now = time(NULL);
    uint64_t base_counter = (uint64_t)(now / time_step);
    
    for (int offset = -clock_skew_tolerance; offset <= clock_skew_tolerance; offset++) {
        uint64_t counter = base_counter + offset;
        
        // Decode base32 secret
        uint8_t secret_bytes[32];
        int secret_len = base32_decode(secret_base32, secret_bytes, sizeof(secret_bytes));
        if (secret_len < 1) {
            continue;
        }
        
        // Convert counter to big-endian 8-byte array
        uint8_t counter_bytes[8];
        uint64_t temp_counter = counter;
        for (int i = 7; i >= 0; i--) {
            counter_bytes[i] = temp_counter & 0xFF;
            temp_counter >>= 8;
        }
        
        // Compute HMAC-SHA1
        uint8_t hmac[20];
        hmac_sha1(secret_bytes, secret_len, counter_bytes, 8, hmac);
        
        // Dynamic truncation
        uint32_t binary = dynamic_truncate(hmac);
        
        // Generate code with specified digits
        uint32_t power = 1;
        for (int i = 0; i < digits; i++) {
            power *= 10;
        }
        
        uint32_t expected_code = binary % power;
        
        if (code_value == expected_code) {
            return true;
        }
    }
    
    return false;
}
