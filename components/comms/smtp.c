#include "smtp.h"
#include <stdio.h>
#include <string.h>
// ... rest of your includes
// 1. DEFINITIONS FIRST (Must be before ANY includes)
#ifdef ESP_PLATFORM

#define MG_ENABLE_MBEDTLS 1
#endif

// 2. STANDARD HEADERS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 3. PROJECT HEADERS
#include "net.h"
#include "smtp.h"
#include "storage_mgr.h"

// 4. PLATFORM SPECIFIC
#ifdef ESP_PLATFORM
// ESP32 handles email via native ESP-TLS
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "mbedtls/base64.h"
static const char *TAG = "SMTP_SERVICE";
#else
// Linux handles email via Curl (Mongoose is used for the Web Server instead)
#include <curl/curl.h>
#endif

static int is_type(const char *val, const char *type) {
  if (!val || !type)
    return 0;
  return (strcasecmp(val, type) == 0);
}

static void smtp_execute_send(config_t *config, const char *target_email,
                              const char *subject, const char *body) {
#ifdef ESP_PLATFORM
  esp_tls_cfg_t cfg = {
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 10000,
  };

  // 2. Fix: Use the 5-argument version of the function
  esp_tls_t *tls = esp_tls_init();
  if (tls == NULL) {
    ESP_LOGE(TAG, "Failed to initialize TLS structure");
    return;
  }

  int ret = esp_tls_conn_new_sync(storage_get_config()->smtp_server,
                                  strlen(storage_get_config()->smtp_server),
                                  storage_get_config()->smtp_port, &cfg, tls);

  if (ret == 1) { // Success
    ESP_LOGI(TAG, "Secure tunnel opened using Certificate Bundle");

    char buf[1024];
    char b64[256];
    size_t n;
    int len;

    // 1. Read Greeting
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 2. EHLO
    snprintf(buf, sizeof(buf), "EHLO sentinel\r\n");
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 3. AUTH LOGIN
    snprintf(buf, sizeof(buf), "AUTH LOGIN\r\n");
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 4. Username
    mbedtls_base64_encode(
        (unsigned char *)b64, sizeof(b64), &n,
        (const unsigned char *)storage_get_config()->smtp_user,
        strlen(storage_get_config()->smtp_user));
    b64[n] = 0;
    snprintf(buf, sizeof(buf), "%s\r\n", b64);
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 5. Password
    mbedtls_base64_encode(
        (unsigned char *)b64, sizeof(b64), &n,
        (const unsigned char *)storage_get_config()->smtp_pass,
        strlen(storage_get_config()->smtp_pass));
    b64[n] = 0;
    snprintf(buf, sizeof(buf), "%s\r\n", b64);
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 6. MAIL FROM
    snprintf(buf, sizeof(buf), "MAIL FROM:<%s>\r\n",
             storage_get_config()->smtp_user);
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 7. RCPT TO
    snprintf(buf, sizeof(buf), "RCPT TO:<%s>\r\n", target_email);
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 8. DATA
    snprintf(buf, sizeof(buf), "DATA\r\n");
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 9. Payload
    snprintf(buf, sizeof(buf),
             "To: %s\r\nFrom: %s\r\nSubject: %s\r\n\r\n%s\r\n.\r\n",
             target_email, storage_get_config()->smtp_user, subject, body);
    if (esp_tls_conn_write(tls, buf, strlen(buf)) < 0)
      goto smtp_err;
    if ((len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1)) < 0)
      goto smtp_err;

    // 10. QUIT
    snprintf(buf, sizeof(buf), "QUIT\r\n");
    esp_tls_conn_write(tls, buf, strlen(buf));

    ESP_LOGI(TAG, "Email sent successfully to %s", target_email);
    goto smtp_end;

  smtp_err:
    ESP_LOGE(TAG, "SMTP Protocol Error");

  smtp_end:

    // 3. Fix: Use esp_tls_conn_destroy instead of delete
    esp_tls_conn_destroy(tls);
  } else {
    ESP_LOGE(TAG, "Failed to connect, error code: %d", ret);
    esp_tls_conn_destroy(tls);
  }
#else
  // --- Linux Implementation using LibCurl ---
  CURL *curl = curl_easy_init();
  if (!curl)
    return;

  struct curl_slist *recipients = NULL;
  char url[256];
  curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");

  snprintf(url, sizeof(url), "smtps://%s:%d", storage_get_config()->smtp_server,
           storage_get_config()->smtp_port);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERNAME, storage_get_config()->smtp_user);
  curl_easy_setopt(curl, CURLOPT_PASSWORD, storage_get_config()->smtp_pass);
  curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
  curl_easy_setopt(curl, CURLOPT_MAIL_FROM, storage_get_config()->smtp_user);

  recipients = curl_slist_append(recipients, target_email);
  curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

  char payload[2048];
  snprintf(payload, sizeof(payload),
           "To: %s\r\nFrom: %s\r\nSubject: %s\r\n\r\n%s", target_email,
           storage_get_config()->smtp_user, subject, body);

  // Note: fmemopen is great for Linux, but we avoid it on ESP32 to stay
  // portable
  FILE *temp = fmemopen((void *)payload, strlen(payload), "r");
  curl_easy_setopt(curl, CURLOPT_READDATA, temp);
  curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

  printf("[SMTP] Linux Attempting delivery to: %s\n", target_email);
  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK)
    fprintf(stderr, "[SMTP] Error: %s\n", curl_easy_strerror(res));
  else
    printf("[SMTP] Success: Message delivered.\n");

  curl_slist_free_all(recipients);
  curl_easy_cleanup(curl);
  fclose(temp);
#endif
}

// ... Rest of your logic (smtp_alert_all_contacts, etc.) stays exactly the same
// ...
/**
 * @brief Orchestrates a mass-alert to the config and all secondary users.
 */
void smtp_alert_all_contacts(config_t *config, zone_t *z) {
  // 1. GATEKEEPER CHECK
  if (storage_get_config()->notify != 2) {
    printf("[SMTP] Skipping email: Notification mode is %d (Expected 2)\n",
           storage_get_config()->notify);
    return;
  }

  char subject[256];
  char body[512];
  snprintf(subject, sizeof(subject), "CRITICAL: Sentinel Alarm - %s by %s",
           z->name, z->description);

  // STEP 1: CATEGORY SELECTION
  if (is_type(z->call, "fire")) {
    snprintf(body, sizeof(body),
             "Active FIRE detected in %s by %s. Please notify local FIRE "
             "authorities.",
             z->name, z->description);
  } else if (is_type(z->call, "police")) {
    snprintf(body, sizeof(body),
             "Security breach detected in %s by %s. Please notify local police "
             "authorities.",
             z->name, z->description);
  } else if (is_type(z->call, "medical")) {
    snprintf(body, sizeof(body),
             "Medical issue detected in %s. Please notify local authorities.",
             z->name);
  } else {
    snprintf(body, sizeof(body),
             "Other breach detected in %s. Please notify local authorities.",
             z->name);
  }

  // 2. Notify the Master Email
  if (storage_get_config()->email[0] != '\0') {
    smtp_execute_send(config, storage_get_config()->email, subject, body);
  }

  // 3. Notify Secondary Users
  for (int i = 0; i < MAX_USERS; i++) {
    // Check if user has Email notifications (2) enabled
    if (storage_get_user(i)->notify == 2) {
      if (storage_get_user(i)->email[0] != '\0') {
        smtp_execute_send(config, storage_get_user(i)->email, subject, body);
      }
    } else {
      printf("[SMTP] Skipping user %s: User-level notify is disabled.\n",
             storage_get_user(i)->name);
    }
  }
}

/**
 * @brief Notifies all parties that the alarm state has ended.
 */
void smtp_send_cancellation(config_t *config) {
  // Global check: only send if system is in Email mode
  if (storage_get_config()->notify != 2)
    return;

  const char *subject = "Sentinel Status: Alarm Cancelled";
  const char *body = "The emergency alarm has been disarmed and cancelled.";

  // 1. Notify Master
  if (storage_get_config()->email[0] != '\0') {
    smtp_execute_send(config, storage_get_config()->email, subject, body);
  }

  // 2. Notify Secondary Users (Checked against individual preferences)
  for (int i = 0; i < MAX_USERS; i++) {
    if (storage_get_user(i)->notify == 2 &&
        storage_get_user(i)->email[0] != '\0') {
      smtp_execute_send(config, storage_get_user(i)->email, subject, body);
    }
  }
}