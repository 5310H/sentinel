/**
 * @file cert_mgr.h
 * @brief HTTPS Certificate Manager for ESP32
 * 
 * Manages loading TLS certificates from persistent storage (SPIFFS/NVS)
 * with fallback to embedded certificate.
 */

#ifndef CERT_MGR_H
#define CERT_MGR_H

#include <stdint.h>
#include "esp_err.h"
#include "mongoose.h"

/**
 * Initialize certificate manager and load certificate from storage
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t cert_mgr_init(void);

/**
 * Get certificate as mg_str for TLS initialization
 * @return mg_str containing the certificate (PEM format)
 */
struct mg_str cert_mgr_get_cert(void);

/**
 * Update certificate in persistent storage from file
 * @param cert_data Certificate PEM data
 * @param cert_len Length of certificate data
 * @return ESP_OK on success
 */
esp_err_t cert_mgr_update(const char *cert_data, size_t cert_len);

/**
 * Load certificate from file path into storage
 * @param file_path Path to server.pem file (e.g., "/spiffs/server.pem")
 * @return ESP_OK on success
 */
esp_err_t cert_mgr_load_from_file(const char *file_path);

/**
 * Restore to embedded fallback certificate
 * @return ESP_OK on success
 */
esp_err_t cert_mgr_reset_to_embedded(void);

/**
 * Get certificate info for diagnostics
 * @param subject Buffer to store subject string
 * @param subject_len Length of subject buffer
 * @return ESP_OK on success
 */
esp_err_t cert_mgr_get_info(char *subject, size_t subject_len);

#endif // CERT_MGR_H
