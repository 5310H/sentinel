#include "password_hash.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
    #include "esp_random.h"
    #include "mbedtls/md.h"
    #include "mbedtls/pkcs5.h"
    #include "esp_log.h"
    static const char *TAG = "PWD_HASH";
#else
    // Linux mock: use standard library
    #include <stdlib.h>
    #include <time.h>
    #include <openssl/sha.h>
    #include <openssl/evp.h>
    #define TAG "PWD_HASH"
    #define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

/**
 * @brief Constant-time memory comparison
 * Prevents timing attacks by comparing all bytes regardless of differences
 */
static bool constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/**
 * @brief Generate random salt using hardware RNG
 */
static void generate_salt(uint8_t *salt, size_t len) {
#ifdef ESP_PLATFORM
    // Use ESP32 hardware RNG
    esp_fill_random(salt, len);
#else
    // Linux: use /dev/urandom
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(salt, 1, len, f);
        fclose(f);
    } else {
        // Fallback to weak RNG (testing only!)
        srand(time(NULL));
        for (size_t i = 0; i < len; i++) {
            salt[i] = rand() & 0xFF;
        }
    }
#endif
}

bool password_hash_create(const char *pin, password_hash_t *out_hash) {
    if (!pin || !out_hash) {
        ESP_LOGE(TAG, "Null pointer in password_hash_create");
        return false;
    }
    
    if (strlen(pin) == 0) {
        ESP_LOGE(TAG, "Empty PIN provided");
        return false;
    }
    
    // Generate random salt
    generate_salt(out_hash->salt, PASSWORD_HASH_SALT_LEN);
    out_hash->iterations = PASSWORD_HASH_ITERATIONS;
    
#ifdef ESP_PLATFORM
    // Use mbedTLS PBKDF2-HMAC-SHA256
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        ESP_LOGE(TAG, "Failed to get SHA256 md_info");
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    int ret = mbedtls_md_setup(&md_ctx, md_info, 1); // 1 = use HMAC
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_md_setup failed: %d", ret);
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    // PBKDF2 derivation
    ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char *)pin, strlen(pin),
        out_hash->salt, PASSWORD_HASH_SALT_LEN,
        out_hash->iterations,
        PASSWORD_HASH_OUTPUT_LEN,
        out_hash->hash
    );
    
    mbedtls_md_free(&md_ctx);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 failed: %d", ret);
        return false;
    }
#else
    // Linux: use OpenSSL PKCS5_PBKDF2_HMAC
    int ret = PKCS5_PBKDF2_HMAC(
        pin, strlen(pin),
        out_hash->salt, PASSWORD_HASH_SALT_LEN,
        out_hash->iterations,
        EVP_sha256(),
        PASSWORD_HASH_OUTPUT_LEN,
        out_hash->hash
    );
    
    if (ret != 1) {
        ESP_LOGE(TAG, "PBKDF2 failed");
        return false;
    }
#endif
    
    ESP_LOGI(TAG, "Password hash created successfully");
    return true;
}

bool password_hash_verify(const char *pin, const password_hash_t *stored_hash) {
    if (!pin || !stored_hash) {
        ESP_LOGE(TAG, "Null pointer in password_hash_verify");
        return false;
    }
    
    if (strlen(pin) == 0) {
        return false;
    }
    
    // Compute hash of input PIN using stored salt
    uint8_t computed_hash[PASSWORD_HASH_OUTPUT_LEN];
    
#ifdef ESP_PLATFORM
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    int ret = mbedtls_md_setup(&md_ctx, md_info, 1);
    if (ret != 0) {
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char *)pin, strlen(pin),
        stored_hash->salt, PASSWORD_HASH_SALT_LEN,
        stored_hash->iterations,
        PASSWORD_HASH_OUTPUT_LEN,
        computed_hash
    );
    
    mbedtls_md_free(&md_ctx);
    
    if (ret != 0) {
        return false;
    }
#else
    int ret = PKCS5_PBKDF2_HMAC(
        pin, strlen(pin),
        stored_hash->salt, PASSWORD_HASH_SALT_LEN,
        stored_hash->iterations,
        EVP_sha256(),
        PASSWORD_HASH_OUTPUT_LEN,
        computed_hash
    );
    
    if (ret != 1) {
        return false;
    }
#endif
    
    // Constant-time comparison to prevent timing attacks
    return constant_time_compare(computed_hash, stored_hash->hash, PASSWORD_HASH_OUTPUT_LEN);
}

char *password_hash_to_hex(const password_hash_t *hash, char *out_hex) {
    if (!hash || !out_hex) return NULL;
    
    // Format: salt(64 hex chars) + hash(64 hex chars) + iterations(8 hex chars) = 136 chars
    char *p = out_hex;
    
    // Salt
    for (int i = 0; i < PASSWORD_HASH_SALT_LEN; i++) {
        sprintf(p, "%02x", hash->salt[i]);
        p += 2;
    }
    
    // Hash
    for (int i = 0; i < PASSWORD_HASH_OUTPUT_LEN; i++) {
        sprintf(p, "%02x", hash->hash[i]);
        p += 2;
    }
    
    // Iterations
    sprintf(p, "%08" PRIx32, hash->iterations);
    
    return out_hex;
}

bool password_hash_from_hex(const char *hex_str, password_hash_t *out_hash) {
    if (!hex_str || !out_hash) return false;
    
    size_t expected_len = (PASSWORD_HASH_SALT_LEN + PASSWORD_HASH_OUTPUT_LEN) * 2 + 8;
    if (strlen(hex_str) != expected_len) {
        ESP_LOGE(TAG, "Invalid hex string length: %zu (expected %zu)", strlen(hex_str), expected_len);
        return false;
    }
    
    const char *p = hex_str;
    
    // Parse salt
    for (int i = 0; i < PASSWORD_HASH_SALT_LEN; i++) {
        unsigned int byte;
        if (sscanf(p, "%2x", &byte) != 1) return false;
        out_hash->salt[i] = (uint8_t)byte;
        p += 2;
    }
    
    // Parse hash
    for (int i = 0; i < PASSWORD_HASH_OUTPUT_LEN; i++) {
        unsigned int byte;
        if (sscanf(p, "%2x", &byte) != 1) return false;
        out_hash->hash[i] = (uint8_t)byte;
        p += 2;
    }
    
    // Parse iterations
    unsigned int iterations;
    if (sscanf(p, "%8x", &iterations) != 1) return false;
    out_hash->iterations = iterations;
    
    return true;
}
