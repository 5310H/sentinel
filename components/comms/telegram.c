#include "telegram.h"
#include "logging.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
static const char *TAG = "TELEGRAM";
#else
#include <cjson/cJSON.h>
#define TAG "TELEGRAM"
#endif

// Non-blocking Telegram send structure
typedef struct {
  char url[512];
  char post_data[1024];
  char token[128];
} telegram_request_t;

#ifdef ESP_PLATFORM

// ESP-IDF HTTP client event handler (thread-safe, async)
static esp_err_t telegram_http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    LOG_ERROR(TAG, "HTTP Client Error");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    LOG_DEBUG(TAG, "HTTP Client Connected");
    break;
  case HTTP_EVENT_HEADER_SENT:
    LOG_DEBUG(TAG, "HTTP Client Header Sent");
    break;
  case HTTP_EVENT_ON_HEADER:
    LOG_DEBUG(TAG, "HTTP Response Header: %.*s", evt->data_len,
              (char *)evt->data);
    break;
  case HTTP_EVENT_ON_DATA:
    if (!esp_http_client_is_chunked_response(evt->client)) {
      LOG_INFO(TAG, "HTTP Response: %.*s", evt->data_len, (char *)evt->data);
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    LOG_DEBUG(TAG, "HTTP Client Finished");
    break;
  case HTTP_EVENT_DISCONNECTED:
    LOG_DEBUG(TAG, "HTTP Client Disconnected");
    break;
  default:
    break;
  }
  return ESP_OK;
}

static void telegram_raw_send_async(const char *token, const char *chat_id,
                                    const char *text) {
  // Validate all inputs
  if (token == NULL || token[0] == '\0' || chat_id == NULL ||
      chat_id[0] == '\0' || text == NULL || text[0] == '\0') {
    return;
  }

  if (strlen(token) < 5 || strlen(chat_id) < 3 || strlen(text) < 1)
    return;

  // Build JSON payload (handles escaping of special chars/newlines
  // automatically)
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return;
  cJSON_AddStringToObject(root, "chat_id", chat_id);
  cJSON_AddStringToObject(root, "text", text);
  cJSON_AddStringToObject(root, "parse_mode", "Markdown");

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data == NULL) {
    cJSON_Delete(root);
    return;
  }

  // URL for Telegram Bot API
  char url[512];
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage",
           token);

  // Configure HTTP client
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_POST,
      .event_handler = telegram_http_event_handler,
      .crt_bundle_attach = esp_crt_bundle_attach, // Use built-in CA bundle
      .timeout_ms = 10000,                        // 10 second timeout
      // .transport_keep_alive_enabled = true,  // Not available in all ESP-IDF
      // versions
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    LOG_ERROR(TAG, "Failed to initialize HTTP client");
    return;
  }

  // Set headers and perform request
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    int status_code = esp_http_client_get_status_code(client);
    LOG_INFO(TAG, "Telegram API Response: HTTP %d", status_code);
  } else {
    LOG_ERROR(TAG, "HTTP Request Failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  free(post_data);
  cJSON_Delete(root);
}

#else

// Linux/Mock: libcurl-based Telegram implementation (simulated async)
#include <curl/curl.h>

// Simple response handler (mock async - runs inline for testing)
static size_t telegram_curl_callback(void *contents, size_t size, size_t nmemb,
                                     void *userp) {
  size_t realsize = size * nmemb;
  printf("[%s] Telegram API Response: %.*s\n", TAG, (int)realsize,
         (char *)contents);
  (void)userp;
  return realsize;
}

static void telegram_raw_send_async(const char *token, const char *chat_id,
                                    const char *text) {
  // Validate all inputs
  if (token == NULL || token[0] == '\0' || chat_id == NULL ||
      chat_id[0] == '\0' || text == NULL || text[0] == '\0') {
    return;
  }

  if (strlen(token) < 5 || strlen(chat_id) < 3 || strlen(text) < 1)
    return;

  // For Linux testing: use curl (simulates async by logging)
  CURL *curl = curl_easy_init();
  char *esc_text = curl_easy_escape(curl, text, 0);
  char *esc_chat = curl_easy_escape(curl, chat_id, 0);

  // Build POST request body (URL-encoded)
  char post_data[2048];
  snprintf(post_data, sizeof(post_data),
           "chat_id=%s&text=%s&parse_mode=Markdown",
           esc_chat ? esc_chat : chat_id, esc_text ? esc_text : text);

  // URL for Telegram Bot API
  char url[512];
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage",
           token);

  if (!curl) {
    printf("[%s] Failed to init curl\n", TAG);
    return;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, telegram_curl_callback);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Skip SSL for testing

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    printf("[%s] Telegram request failed: %s\n", TAG, curl_easy_strerror(res));
  }

  if (esc_text)
    curl_free(esc_text);
  if (esc_chat)
    curl_free(esc_chat);
  curl_easy_cleanup(curl);
  printf("[%s] Telegram message sent (async mock)\n", TAG);
}

#endif

void telegram_send_alert(config_t *conf, zone_t *z) {
  if (conf == NULL || z == NULL)
    return;

  char token[STR_MEDIUM] = {0};
  char chat_id[STR_SMALL] = {0};
  char z_name[STR_SMALL] = {0};
  char z_call[STR_SMALL] = {0};
  char z_desc[STR_MEDIUM] = {0};

  storage_lock();
  if (z->name[0] == '\0') {
    storage_unlock();
    return;
  }
  strncpy(token, conf->telegram_token, sizeof(token) - 1);
  strncpy(chat_id, conf->telegram_id, sizeof(chat_id) - 1);
  strncpy(z_name, z->name, sizeof(z_name) - 1);
  strncpy(z_call, z->call, sizeof(z_call) - 1);
  strncpy(z_desc, z->description, sizeof(z_desc) - 1);
  storage_unlock();

  char msg[512];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char time_str[32];
  strftime(time_str, sizeof(time_str) - 1, "%H:%M:%S", t);

  snprintf(msg, sizeof(msg),
           "🚨 *SENTINEL ALARM ALERT* 🚨\n\n"
           "*Zone:* %.50s\n"
           "*Call:* %.30s\n"
           "*Description:* %.100s\n"
           "*Time:* %s\n"
           "*Status:* VIOLATED\n\n"
           "Please check alarm dashboard and call appropriate authorities.",
           z_name, z_call, z_desc, time_str);

  telegram_raw_send_async(token, chat_id, msg);
}

void telegram_send_cancellation(config_t *conf) {
  if (conf == NULL)
    return;

  char token[STR_MEDIUM] = {0};
  char chat_id[STR_SMALL] = {0};
  char site_name[STR_MEDIUM] = {0};

  storage_lock();
  if (conf->name[0] == '\0') {
    storage_unlock();
    return;
  }
  strncpy(token, conf->telegram_token, sizeof(token) - 1);
  strncpy(chat_id, conf->telegram_id, sizeof(chat_id) - 1);
  strncpy(site_name, conf->name, sizeof(site_name) - 1);
  storage_unlock();

  char msg[256];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char time_str[32];
  strftime(time_str, sizeof(time_str) - 1, "%H:%M:%S", t);

  snprintf(msg, sizeof(msg),
           "✅ *SYSTEM DISARMED*\n"
           "*Site:* %.50s\n"
           "*Status:* All alarms cleared.\n"
           "*Time:* %s",
           site_name, time_str);

  telegram_raw_send_async(token, chat_id, msg);
}