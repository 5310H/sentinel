#include "noonlight.h"
#include "cJSON.h"
#include "storage_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "net.h"
static const char *TAG = "NOONLIGHT";
#else
#include <curl/curl.h>
#endif

#ifndef ESP_PLATFORM
static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *userp) {
  // ... existing callback code ...
  size_t realsize = size * nmemb;
  char *response = (char *)userp;
  if (strlen(response) + realsize < 2047) {
    strncat(response, (char *)contents, realsize);
  }
  return realsize;
}
#endif
static bool do_request(config_t *conf, const char *url, const char *method,
                       const char *data, char *out_buffer, size_t out_len) {
#ifdef ESP_PLATFORM
  // ADD THIS LINE TO USE THE TAG
  ESP_LOGI(TAG, "Request: %s %s", method, url);
  esp_http_client_config_t http_conf = {
      .url = url,
      .method = (strcmp(method, "POST") == 0)    ? HTTP_METHOD_POST
                : (strcmp(method, "PATCH") == 0) ? HTTP_METHOD_PATCH
                                                 : HTTP_METHOD_GET,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 5000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&http_conf);
  char auth_header[512];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s",
           conf->monitor_service_key);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Authorization", auth_header);
  if (data)
    esp_http_client_set_post_field(client, data, strlen(data));

  esp_err_t err = esp_http_client_perform(client);
  bool success = false;
  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    if (status >= 200 && status < 300) {
      if (out_buffer) {
        int read =
            esp_http_client_read_response(client, out_buffer, out_len - 1);
        if (read >= 0)
          out_buffer[read] = 0;
      }
      success = true;
    }
  }
  esp_http_client_cleanup(client);
  return success;
#else
  CURL *curl = curl_easy_init();
  struct curl_slist *headers = NULL;
  bool success = false;
  if (curl) {
    char auth[512];
    headers = curl_slist_append(headers, "Content-type: application/json");
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
             conf->monitor_service_key);
    headers = curl_slist_append(headers, auth);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (strcmp(method, "PATCH") == 0)
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    if (data)
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    if (out_buffer) {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_buffer);
    }
    success = (curl_easy_perform(curl) == CURLE_OK);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }
  return success;
#endif
}

// --- 1. CREATE ALARM ---
bool noonlight_create_alarm(config_t *conf, zone_t *z, int slot) {
  char response_buffer[2048] = {0};

  cJSON *req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "name", conf->name);
  cJSON_AddStringToObject(req, "phone", conf->phone);
  cJSON_AddStringToObject(req, "owner_id", conf->account_id);

  cJSON *loc = cJSON_CreateObject();
  cJSON_AddStringToObject(loc, "address1", conf->address1);
  cJSON_AddStringToObject(loc, "city", conf->city);
  cJSON_AddStringToObject(loc, "state", conf->state);
  cJSON_AddStringToObject(loc, "zipcode", conf->zip_code);
  cJSON_AddItemToObject(req, "location", loc);

  cJSON *svc = cJSON_CreateObject();
  cJSON_AddBoolToObject(svc, "police", slot == 0);
  cJSON_AddBoolToObject(svc, "fire", slot == 1);
  cJSON_AddBoolToObject(svc, "medical", slot == 2);
  cJSON_AddBoolToObject(svc, "other", slot == 3);
  cJSON_AddItemToObject(req, "services", svc);

  char *data = cJSON_PrintUnformatted(req);
  bool success = false;

  if (do_request(conf, conf->monitoring_url, "POST", data, response_buffer,
                 2048)) {
    cJSON *root = cJSON_Parse(response_buffer);
    if (root) {
      cJSON *id = cJSON_GetObjectItem(root, "id");
      if (id && id->valuestring) {
        storage_lock();
        strncpy(conf->nl_ids[slot], id->valuestring, 63);
        storage_save_config();
        storage_unlock();
        noonlight_log_event(conf, z, id->valuestring);
        noonlight_send_instructions(conf, id->valuestring);
        success = true;
      }
      cJSON_Delete(root);
    }
  }

  free(data);
  cJSON_Delete(req);
  return success;
}

// --- 2. LOG EVENT ---
void noonlight_log_event(config_t *conf, zone_t *z, const char *alarm_id) {
  if (!alarm_id || strlen(alarm_id) == 0)
    return;
  char url[256];
  snprintf(url, sizeof(url), "%s/%s/events", conf->monitoring_url, alarm_id);

  cJSON *arr = cJSON_CreateArray();
  cJSON *evt = cJSON_CreateObject();
  cJSON_AddStringToObject(evt, "event_type", "alarm.device.activated_alarm");

  cJSON *meta = cJSON_CreateObject();
  cJSON_AddStringToObject(meta, "attribute", z->call);
  cJSON_AddStringToObject(meta, "value", "tripped");
  cJSON_AddStringToObject(meta, "device_name", z->name);
  cJSON_AddItemToObject(evt, "meta", meta);
  cJSON_AddItemToArray(arr, evt);

  char *data = cJSON_PrintUnformatted(arr);
  do_request(conf, url, "POST", data, NULL, 0);
  free(data);
  cJSON_Delete(arr);
}

// --- 3. SEND INSTRUCTIONS ---
void noonlight_send_instructions(config_t *conf, const char *alarm_id) {
  if (!alarm_id || strlen(conf->instructions) == 0)
    return;
  char url[256];
  snprintf(url, sizeof(url), "%s/%s/instructions", conf->monitoring_url,
           alarm_id);

  // FIX: Use proper nested instructions array structure per API spec
  cJSON *root = cJSON_CreateObject();
  cJSON *instructions_array = cJSON_CreateArray();
  cJSON *instruction_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(instruction_obj, "type", "entry");
  cJSON_AddStringToObject(instruction_obj, "text", conf->instructions);
  cJSON_AddItemToArray(instructions_array, instruction_obj);
  cJSON_AddItemToObject(root, "instructions", instructions_array);

  char *data = cJSON_PrintUnformatted(root);
  do_request(conf, url, "POST", data, NULL, 0);
  free(data);
  cJSON_Delete(root);
}

// --- 4. SYNC PEOPLE ---
void noonlight_sync_people(config_t *conf, user_t *users_list, int count,
                           const char *alarm_id) {
  if (!alarm_id)
    return;
  for (int i = 0; i < count; i++) {
    char url[256];
    snprintf(url, sizeof(url), "%s/%s/people", conf->monitoring_url, alarm_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", users_list[i].name);
    cJSON_AddStringToObject(root, "phone", users_list[i].phone);
    // Noonlight requires text PIN for emergency responder verification
    // Use dedicated emergency_pin field (not login PIN)
    if (strlen(users_list[i].emergency_pin) > 0) {
      cJSON_AddStringToObject(root, "pin", users_list[i].emergency_pin);
    }

    char *data = cJSON_PrintUnformatted(root);
    do_request(conf, url, "POST", data, NULL, 0);
    free(data);
    cJSON_Delete(root);
  }
}

// --- 5. CANCEL ---
bool noonlight_cancel_alarm(config_t *conf, const char *id) {
  if (!id || strlen(id) == 0)
    return false;
  char url[256];
  snprintf(url, sizeof(url), "%s/%s/status", conf->monitoring_url, id);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "status",
                          "CANCELED"); // FIX: Must be uppercase per API spec
  cJSON_AddStringToObject(root, "pin", conf->pin);

  char *data = cJSON_PrintUnformatted(root);
  bool res = do_request(conf, url, "POST", data, NULL,
                        0); // FIX: API requires POST, not PATCH
  free(data);
  cJSON_Delete(root);
  return res;
}

// --- 6. STATUS SYNC ---
int os_get_noonlight_status(const char *alarm_id) {
  if (!alarm_id || strlen(alarm_id) == 0)
    return 0;
  char response_buffer[2048] = {0};
  char url[256];
  int result_status = 1;
  snprintf(url, sizeof(url), "%s/%s/status",
           storage_get_config()->monitoring_url, alarm_id);
  if (do_request(storage_get_config(), url, "GET", NULL, response_buffer,
                 2048)) {
    cJSON *root = cJSON_Parse(response_buffer);
    if (root) {
      cJSON *st = cJSON_GetObjectItem(root, "status");
      if (st && st->valuestring && strcmp(st->valuestring, "CANCELED") == 0)
        result_status = 2;
      cJSON_Delete(root);
    }
  }
  return result_status;
}

void noonlight_sync_task(config_t *conf) {
  if (!conf)
    return;

  storage_lock();
  int notify_val = conf->notify;
  char ids[MAX_ALARM_SLOTS][64] = {0};
  for (int i = 0; i < MAX_ALARM_SLOTS; i++) {
    strncpy(ids[i], conf->nl_ids[i], 63);
  }
  storage_unlock();

  if (notify_val != 4)
    return;

  for (int i = 0; i < MAX_ALARM_SLOTS; i++) {
    if (strlen(ids[i]) > 0) {
      if (os_get_noonlight_status(ids[i]) == 2) {
        printf("[NOONLIGHT] Clearing Canceled Slot %d\n", i);
        storage_lock();
        memset(conf->nl_ids[i], 0, 64);
        storage_save_config();
        storage_unlock();
      }
    }
  }
}