#include "session_mgr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
    #include "esp_random.h"
    #include "mbedtls/md.h"
    #include "mbedtls/base64.h"
    #include "nvs_flash.h"
    #include "nvs.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    static const char *TAG = "SESSION_MGR";
#else
    #include <time.h>
    #include <openssl/hmac.h>
    #include <openssl/evp.h>
    #define TAG "SESSION_MGR"
    #define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

// Internal state
static session_t sessions[SESSION_MAX_ACTIVE];
static uint8_t secret_key[SESSION_SECRET_KEY_LEN];
static bool initialized = false;

// JWT helper functions
static void base64_url_encode(const uint8_t *src, size_t src_len, char *dst, size_t dst_len);
static bool base64_url_decode(const char *src, uint8_t *dst, size_t *dst_len);
static void hmac_sha256(const uint8_t *key, size_t key_len, const char *data, size_t data_len, uint8_t *out);

bool session_mgr_init(void) {
    if (initialized) return true;
    
    memset(sessions, 0, sizeof(sessions));
    
#ifdef ESP_PLATFORM
    // Load or generate secret key from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("session", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return false;
    }
    
    size_t key_len = SESSION_SECRET_KEY_LEN;
    err = nvs_get_blob(nvs_handle, "secret_key", secret_key, &key_len);
    if (err != ESP_OK || key_len != SESSION_SECRET_KEY_LEN) {
        // Generate new secret key
        esp_fill_random(secret_key, SESSION_SECRET_KEY_LEN);
        nvs_set_blob(nvs_handle, "secret_key", secret_key, SESSION_SECRET_KEY_LEN);
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Generated new session secret key");
    } else {
        ESP_LOGI(TAG, "Loaded existing session secret key");
    }
    
    nvs_close(nvs_handle);
#else
    // Linux: generate random key (not persistent)
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(secret_key, 1, SESSION_SECRET_KEY_LEN, f);
        fclose(f);
    }
#endif
    
    initialized = true;
    ESP_LOGI(TAG, "Session manager initialized");
    return true;
}

bool session_create(const char *username, const char *ip_address, char *out_token) {
    if (!initialized || !username || !out_token) return false;
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
        if (!sessions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        ESP_LOGW(TAG, "Max sessions reached, cleaning up expired");
        session_cleanup_expired();
        // Try again
        for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
            if (!sessions[i].active) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            ESP_LOGE(TAG, "No available session slots");
            return false;
        }
    }
    
    session_t *sess = &sessions[slot];
    memset(sess, 0, sizeof(session_t));
    
    strncpy(sess->username, username, SESSION_USERNAME_MAX_LEN - 1);
    if (ip_address) {
        strncpy(sess->ip_address, ip_address, SESSION_IP_MAX_LEN - 1);
    }
    sess->issued_at = time(NULL);
    sess->expires_at = sess->issued_at + SESSION_EXPIRY_SECONDS;
    sess->active = true;
    
    // Build JWT: header.payload.signature
    // Header: {"alg":"HS256","typ":"JWT"}
    const char *header_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    
    // Payload: {"sub":"username","iat":1234567890,"exp":1234571490,"ip":"192.168.1.100"}
    char payload_json[256];
    snprintf(payload_json, sizeof(payload_json),
        "{\"sub\":\"%s\",\"iat\":%ld,\"exp\":%ld,\"ip\":\"%s\"}",
        username, (long)sess->issued_at, (long)sess->expires_at,
        ip_address ? ip_address : "");
    
    // Base64URL encode header and payload
    char header_b64[128], payload_b64[384];
    base64_url_encode((uint8_t*)header_json, strlen(header_json), header_b64, sizeof(header_b64));
    base64_url_encode((uint8_t*)payload_json, strlen(payload_json), payload_b64, sizeof(payload_b64));
    
    // Create signing input: header.payload
    char signing_input[512];
    snprintf(signing_input, sizeof(signing_input), "%s.%s", header_b64, payload_b64);
    
    // Compute HMAC-SHA256 signature
    uint8_t signature[32];
    hmac_sha256(secret_key, SESSION_SECRET_KEY_LEN, signing_input, strlen(signing_input), signature);
    
    // Base64URL encode signature
    char signature_b64[64];
    base64_url_encode(signature, 32, signature_b64, sizeof(signature_b64));
    
    // Final token: header.payload.signature
    size_t header_len = strlen(header_b64);
    size_t payload_len = strlen(payload_b64);
    size_t sig_len = strlen(signature_b64);
    if (header_len + payload_len + sig_len + 3 > SESSION_TOKEN_MAX_LEN) {
        ESP_LOGE(TAG, "Token too large");
        return false;
    }
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(sess->token, SESSION_TOKEN_MAX_LEN, "%s.%s.%s", header_b64, payload_b64, signature_b64);
    #pragma GCC diagnostic pop
    strncpy(out_token, sess->token, SESSION_TOKEN_MAX_LEN - 1);
    out_token[SESSION_TOKEN_MAX_LEN - 1] = '\0';
    
    ESP_LOGI(TAG, "Created session for user: %s (expires in %d sec)", username, SESSION_EXPIRY_SECONDS);
    return true;
}

bool session_validate(const char *token, const char *client_ip, char *out_username) {
    if (!initialized || !token || !out_username) return false;
    
    // Parse token: header.payload.signature
    char token_copy[SESSION_TOKEN_MAX_LEN];
    strncpy(token_copy, token, SESSION_TOKEN_MAX_LEN - 1);
    
    char *header = strtok(token_copy, ".");
    char *payload = strtok(NULL, ".");
    char *signature = strtok(NULL, ".");
    
    if (!header || !payload || !signature) {
        ESP_LOGW(TAG, "Malformed token");
        return false;
    }
    
    // Rebuild signing input
    char signing_input[512];
    snprintf(signing_input, sizeof(signing_input), "%s.%s", header, payload);
    
    // Compute expected signature
    uint8_t expected_signature[32];
    hmac_sha256(secret_key, SESSION_SECRET_KEY_LEN, signing_input, strlen(signing_input), expected_signature);
    
    char expected_signature_b64[64];
    base64_url_encode(expected_signature, 32, expected_signature_b64, sizeof(expected_signature_b64));
    
    // Constant-time comparison
    if (strcmp(signature, expected_signature_b64) != 0) {
        ESP_LOGW(TAG, "Invalid token signature");
        return false;
    }
    
    // Decode payload to check expiration
    uint8_t payload_decoded[384];
    size_t payload_len = sizeof(payload_decoded);
    if (!base64_url_decode(payload, payload_decoded, &payload_len)) {
        ESP_LOGW(TAG, "Failed to decode payload");
        return false;
    }
    payload_decoded[payload_len] = '\0';
    
    // Parse JSON payload (simple parser for our known format)
    char username[SESSION_USERNAME_MAX_LEN] = "";
    time_t exp = 0;
    char ip[SESSION_IP_MAX_LEN] = "";
    
    // Extract fields (simple string parsing, not full JSON parser)
    char *sub_ptr = strstr((char*)payload_decoded, "\"sub\":\"");
    if (sub_ptr) {
        sub_ptr += 7;
        char *end = strchr(sub_ptr, '"');
        if (end) {
            size_t len = end - sub_ptr;
            if (len < SESSION_USERNAME_MAX_LEN) {
                memcpy(username, sub_ptr, len);
                username[len] = '\0';
            }
        }
    }
    
    char *exp_ptr = strstr((char*)payload_decoded, "\"exp\":");
    if (exp_ptr) {
        sscanf(exp_ptr + 6, "%ld", (long*)&exp);
    }
    
    char *ip_ptr = strstr((char*)payload_decoded, "\"ip\":\"");
    if (ip_ptr) {
        ip_ptr += 6;
        char *end = strchr(ip_ptr, '"');
        if (end) {
            size_t len = end - ip_ptr;
            if (len < SESSION_IP_MAX_LEN) {
                memcpy(ip, ip_ptr, len);
                ip[len] = '\0';
            }
        }
    }
    
    // Check expiration
    if (exp < time(NULL)) {
        ESP_LOGW(TAG, "Token expired");
        return false;
    }
    
    // Check IP binding if requested
    if (client_ip && strlen(ip) > 0 && strcmp(ip, client_ip) != 0) {
        ESP_LOGW(TAG, "Token IP mismatch");
        return false;
    }
    
    // Check if session is active
    bool found = false;
    for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
        if (sessions[i].active && strcmp(sessions[i].token, token) == 0) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        ESP_LOGW(TAG, "Session not found or revoked");
        return false;
    }
    
    strcpy(out_username, username);
    return true;
}

bool session_revoke(const char *token) {
    if (!initialized || !token) return false;
    
    for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
        if (sessions[i].active && strcmp(sessions[i].token, token) == 0) {
            sessions[i].active = false;
            ESP_LOGI(TAG, "Revoked session for user: %s", sessions[i].username);
            return true;
        }
    }
    
    return false;
}

int session_revoke_all_for_user(const char *username) {
    if (!initialized || !username) return 0;
    
    int count = 0;
    for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
        if (sessions[i].active && strcmp(sessions[i].username, username) == 0) {
            sessions[i].active = false;
            count++;
        }
    }
    
    if (count > 0) {
        ESP_LOGI(TAG, "Revoked %d sessions for user: %s", count, username);
    }
    
    return count;
}

int session_cleanup_expired(void) {
    if (!initialized) return 0;
    
    time_t now = time(NULL);
    int count = 0;
    
    for (int i = 0; i < SESSION_MAX_ACTIVE; i++) {
        if (sessions[i].active && sessions[i].expires_at < now) {
            sessions[i].active = false;
            count++;
        }
    }
    
    if (count > 0) {
        ESP_LOGI(TAG, "Cleaned up %d expired sessions", count);
    }
    
    return count;
}

int session_list_active(session_t *out_sessions, int max_sessions) {
    if (!initialized || !out_sessions) return 0;
    
    int count = 0;
    for (int i = 0; i < SESSION_MAX_ACTIVE && count < max_sessions; i++) {
        if (sessions[i].active) {
            memcpy(&out_sessions[count], &sessions[i], sizeof(session_t));
            count++;
        }
    }
    
    return count;
}

// ==================== JWT Helper Functions ====================

static void base64_url_encode(const uint8_t *src, size_t src_len, char *dst, size_t dst_len) {
#ifdef ESP_PLATFORM
    size_t olen;
    mbedtls_base64_encode((unsigned char*)dst, dst_len, &olen, src, src_len);
    dst[olen] = '\0';
#else
    EVP_EncodeBlock((unsigned char*)dst, src, src_len);
#endif
    
    // Convert to URL-safe: + → -, / → _, remove trailing =
    for (char *p = dst; *p; p++) {
        if (*p == '+') *p = '-';
        else if (*p == '/') *p = '_';
        else if (*p == '=') { *p = '\0'; break; }
    }
}

static bool base64_url_decode(const char *src, uint8_t *dst, size_t *dst_len) {
    // Convert from URL-safe back to standard Base64
    char temp[512];
    strncpy(temp, src, sizeof(temp) - 1);
    for (char *p = temp; *p; p++) {
        if (*p == '-') *p = '+';
        else if (*p == '_') *p = '/';
    }
    
    // Add padding if needed
    size_t len = strlen(temp);
    while (len % 4 != 0) {
        temp[len++] = '=';
        temp[len] = '\0';
    }
    
#ifdef ESP_PLATFORM
    int ret = mbedtls_base64_decode(dst, *dst_len, dst_len, (unsigned char*)temp, strlen(temp));
    return (ret == 0);
#else
    int ret = EVP_DecodeBlock(dst, (unsigned char*)temp, strlen(temp));
    if (ret < 0) return false;
    *dst_len = ret;
    return true;
#endif
}

static void hmac_sha256(const uint8_t *key, size_t key_len, const char *data, size_t data_len, uint8_t *out) {
#ifdef ESP_PLATFORM
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, (unsigned char*)data, data_len);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
#else
    HMAC(EVP_sha256(), key, key_len, (unsigned char*)data, data_len, out, NULL);
#endif
}
