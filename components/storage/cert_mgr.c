/**
 * @file cert_mgr.c
 * @brief HTTPS Certificate Manager Implementation
 */

#include "cert_mgr.h"
#include "server_cert.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CERT_MGR";

// NVS namespace for certificate storage
#define NVS_CERT_NAMESPACE "certs"
#define NVS_CERT_KEY "server_pem"
#define NVS_CERT_LEN_KEY "server_len"

// Maximum certificate size in NVS (typical cert ~1.5KB, key ~1.7KB, so 4KB is safe)
#define MAX_CERT_SIZE 4096

// Global certificate buffer
static char g_cert_data[MAX_CERT_SIZE];
static size_t g_cert_len = 0;
static bool g_using_embedded = false;

/**
 * Load certificate from NVS storage
 */
static esp_err_t load_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_CERT_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, will use embedded cert");
        return ret;
    }

    size_t required_size = 0;
    ret = nvs_get_blob(nvs_handle, NVS_CERT_KEY, NULL, &required_size);
    
    if (ret == ESP_OK && required_size > 0 && required_size <= MAX_CERT_SIZE) {
        ret = nvs_get_blob(nvs_handle, NVS_CERT_KEY, g_cert_data, &required_size);
        if (ret == ESP_OK) {
            g_cert_len = required_size;
            g_using_embedded = false;
            ESP_LOGI(TAG, "Loaded certificate from NVS (%zu bytes)", g_cert_len);
            nvs_close(nvs_handle);
            return ESP_OK;
        }
    }
    
    nvs_close(nvs_handle);
    return ESP_FAIL;
}

/**
 * Load certificate from file (for setup/initialization)
 */
static esp_err_t load_from_file(const char *file_path) {
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open certificate file: %s", file_path);
        return ESP_FAIL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size > MAX_CERT_SIZE) {
        ESP_LOGE(TAG, "Certificate file too large: %zu > %d", file_size, MAX_CERT_SIZE);
        fclose(f);
        return ESP_FAIL;
    }

    // Read file
    size_t read_size = fread(g_cert_data, 1, file_size, f);
    fclose(f);

    if (read_size != file_size) {
        ESP_LOGE(TAG, "Failed to read certificate file");
        return ESP_FAIL;
    }

    g_cert_len = file_size;
    g_using_embedded = false;
    ESP_LOGI(TAG, "Loaded certificate from file: %s (%zu bytes)", file_path, g_cert_len);
    return ESP_OK;
}

/**
 * Load embedded fallback certificate
 */
static void load_embedded(void) {
    memcpy(g_cert_data, server_cert_pem, server_cert_pem_len - 1);
    g_cert_len = server_cert_pem_len - 1;
    g_using_embedded = true;
    ESP_LOGW(TAG, "Using embedded fallback certificate (%zu bytes)", g_cert_len);
}

/**
 * Save certificate to NVS
 */
static esp_err_t save_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_CERT_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, NVS_CERT_KEY, g_cert_data, g_cert_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write certificate to NVS");
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_set_u32(nvs_handle, NVS_CERT_LEN_KEY, (uint32_t)g_cert_len);
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Certificate saved to NVS");
    }
    return ret;
}

/**
 * Initialize certificate manager
 */
esp_err_t cert_mgr_init(void) {
    ESP_LOGI(TAG, "Initializing certificate manager");

    // Try to load from NVS first
    if (load_from_nvs() == ESP_OK) {
        return ESP_OK;
    }

    // Try to load from SPIFFS
    ESP_LOGI(TAG, "Certificate not in NVS, checking SPIFFS...");
    if (load_from_file("/spiffs/server.pem") == ESP_OK) {
        // Save to NVS for future use
        if (save_to_nvs() == ESP_OK) {
            ESP_LOGI(TAG, "Certificate cached in NVS for faster startup");
        }
        return ESP_OK;
    }

    // Fallback to embedded certificate
    ESP_LOGW(TAG, "Certificate not found in storage, using embedded fallback");
    load_embedded();
    return ESP_OK;  // Not an error - embedded cert is always available
}

/**
 * Get certificate as mg_str
 */
struct mg_str cert_mgr_get_cert(void) {
    return mg_str_n(g_cert_data, g_cert_len);
}

/**
 * Update certificate in storage
 */
esp_err_t cert_mgr_update(const char *cert_data, size_t cert_len) {
    if (cert_len > MAX_CERT_SIZE) {
        ESP_LOGE(TAG, "Certificate too large: %zu > %d", cert_len, MAX_CERT_SIZE);
        return ESP_FAIL;
    }

    memcpy(g_cert_data, cert_data, cert_len);
    g_cert_len = cert_len;
    g_using_embedded = false;

    esp_err_t ret = save_to_nvs();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Certificate updated successfully");
    }
    return ret;
}

/**
 * Load certificate from file
 */
esp_err_t cert_mgr_load_from_file(const char *file_path) {
    esp_err_t ret = load_from_file(file_path);
    if (ret == ESP_OK) {
        // Also save to NVS
        ret = save_to_nvs();
    }
    return ret;
}

/**
 * Reset to embedded certificate
 */
esp_err_t cert_mgr_reset_to_embedded(void) {
    load_embedded();
    // Clear NVS entry
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_CERT_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, NVS_CERT_KEY);
        nvs_erase_key(nvs_handle, NVS_CERT_LEN_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    ESP_LOGI(TAG, "Reset to embedded certificate");
    return ESP_OK;
}

/**
 * Get certificate info
 */
esp_err_t cert_mgr_get_info(char *subject, size_t subject_len) {
    if (subject_len < 32) {
        return ESP_FAIL;
    }
    
    if (g_using_embedded) {
        snprintf(subject, subject_len, "Embedded (valid until 2036-01-25)");
    } else {
        snprintf(subject, subject_len, "Loaded from storage (%zu bytes)", g_cert_len);
    }
    return ESP_OK;
}
