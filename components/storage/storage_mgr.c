#include "storage_mgr.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "cert_mgr.h" // Certificate manager
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
static const char *TAG = "STORAGE_MGR";
#define BASE_PATH "/spiffs"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#ifndef GPIO_NUM_41
#define GPIO_NUM_41 41
#endif
// KC868-A8 V3 SD Card SPI Pins
#define SD_MISO GPIO_NUM_41
#define SD_MOSI GPIO_NUM_2
#define SD_SCLK GPIO_NUM_1
#define SD_CS GPIO_NUM_9
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
#define BASE_PATH "data"
#endif

// Global instances defined in .h (now static)
static config_t config;
static relay_t relays[MAX_RELAYS];
static user_t users[MAX_USERS];
static zone_t zones[MAX_ZONES];
static esphome_device_t esphome_devices[32];
static network_t net_cfg;

#ifdef ESP_PLATFORM
static SemaphoreHandle_t storage_mutex = NULL;
#endif

static int z_count = 0, r_count = 0, u_count = 0, esphome_count = 0;

// --- ACCESSOR FUNCTIONS ---
config_t *storage_get_config(void) { return &config; }
network_t *storage_get_network(void) { return &net_cfg; }
int storage_get_zone_count(void) {
  storage_lock();
  int count = z_count;
  storage_unlock();
  return count;
}

void storage_set_zone_count(int count) {
  storage_lock();
  if (count >= 0 && count <= MAX_ZONES) {
    z_count = count;
  }
  storage_unlock();
}

zone_t *storage_get_zone(int index) {
  if (index >= 0 && index < MAX_ZONES) {
    return &zones[index];
  }
  return NULL;
}

int storage_get_relay_count(void) {
  storage_lock();
  int count = r_count;
  storage_unlock();
  return count;
}

void storage_set_relay_count(int count) {
  storage_lock();
  if (count >= 0 && count <= MAX_RELAYS) {
    r_count = count;
  }
  storage_unlock();
}

relay_t *storage_get_relay(int index) {
  if (index >= 0 && index < MAX_RELAYS) {
    return &relays[index];
  }
  return NULL;
}

int storage_get_user_count(void) {
  storage_lock();
  int count = u_count;
  storage_unlock();
  return count;
}

void storage_set_user_count(int count) {
  storage_lock();
  if (count >= 0 && count <= MAX_USERS) {
    u_count = count;
  }
  storage_unlock();
}

user_t *storage_get_user(int index) {
  if (index >= 0 && index < MAX_USERS) {
    return &users[index];
  }
  return NULL;
}

int storage_get_esphome_count(void) {
  storage_lock();
  int count = esphome_count;
  storage_unlock();
  return count;
}

void storage_set_esphome_count(int count) {
  storage_lock();
  if (count >= 0 && count <= 32) {
    esphome_count = count;
  }
  storage_unlock();
}

esphome_device_t *storage_get_esphome_device(int index) {
  if (index >= 0 && index < 32) {
    return &esphome_devices[index];
  }
  return NULL;
}

void storage_lock(void) {
#ifdef ESP_PLATFORM
  if (storage_mutex)
    xSemaphoreTakeRecursive(storage_mutex, portMAX_DELAY);
#endif
}

void storage_unlock(void) {
#ifdef ESP_PLATFORM
  if (storage_mutex)
    xSemaphoreGiveRecursive(storage_mutex);
#endif
}

// --- INTERNAL HELPERS ---

static void get_full_path(char *dest, const char *filename) {
  const char *offset =
      (strncmp(filename, "data/", 5) == 0) ? filename + 5 : filename;
  snprintf(dest, 128, "%s/%s", BASE_PATH, offset);
}

static void safe_strncpy(char *dest, cJSON *obj, size_t n) {
  if (obj && obj->valuestring) {
    strncpy(dest, obj->valuestring, n - 1);
    dest[n - 1] = '\0';
  } else {
    dest[0] = '\0';
  }
}

static int safe_get_int(cJSON *obj, int fallback) {
  if (!obj || obj->type != cJSON_Number)
    return fallback;
  return obj->valueint;
}

static uint32_t safe_get_uint32(cJSON *obj, uint32_t fallback) {
  if (!obj || obj->type != cJSON_Number)
    return fallback;
  if (obj->valuedouble > UINT32_MAX)
    return UINT32_MAX;
  return (uint32_t)obj->valuedouble;
}

static bool safe_get_bool(cJSON *obj) {
  if (!obj)
    return false;
  return cJSON_IsTrue(obj) || (obj->type == cJSON_Number && obj->valueint == 1);
}

// --- VALIDATION HELPERS ---

static bool validate_config(void) {
  // Check critical fields exist
  if (config.pin[0] == '\0' || strlen(config.pin) < 4) {
#ifdef ESP_PLATFORM
    ESP_LOGW(TAG, "Warning: Master PIN not set or too short");
#endif
    return false;
  }

  // Validate timing delays (in seconds, reasonable ranges)
  if (config.entry_delay < 0 || config.entry_delay > 300) {
    config.entry_delay = 30; // Default
  }
  if (config.exit_delay < 0 || config.exit_delay > 300) {
    config.exit_delay = 60; // Default
  }
  if (config.cancel_delay < 0 || config.cancel_delay > 600) {
    config.cancel_delay = 180; // Default
  }
  if (config.watchdog_esphome_sec < 5 || config.watchdog_esphome_sec > 600) {
    config.watchdog_esphome_sec = 15; // Default 15s
  }
  if (config.watchdog_camera_fails < 1 || config.watchdog_camera_fails > 20) {
    config.watchdog_camera_fails = 3; // Default 3 fails
  }
  if (config.watchdog_network_sec < 10 || config.watchdog_network_sec > 1200) {
    config.watchdog_network_sec = 30; // Default 30s
  }

  return true;
}

// --- ENUM CONVERSION FUNCTIONS ---

zone_type_t zone_type_from_string(const char *type_str) {
  if (type_str == NULL)
    return ZONE_TYPE_UNKNOWN;

  if (strcmp(type_str, "door") == 0)
    return ZONE_TYPE_DOOR;
  if (strcmp(type_str, "window") == 0)
    return ZONE_TYPE_WINDOW;
  if (strcmp(type_str, "motion") == 0)
    return ZONE_TYPE_MOTION;
  if (strcmp(type_str, "camera") == 0)
    return ZONE_TYPE_CAMERA;
  if (strcmp(type_str, "glass_break") == 0)
    return ZONE_TYPE_GLASS_BREAK;
  if (strcmp(type_str, "fire") == 0)
    return ZONE_TYPE_FIRE;
  if (strcmp(type_str, "smoke") == 0)
    return ZONE_TYPE_SMOKE;
  if (strcmp(type_str, "panic") == 0)
    return ZONE_TYPE_PANIC;
  if (strcmp(type_str, "medical") == 0)
    return ZONE_TYPE_MEDICAL;
  if (strcmp(type_str, "temp") == 0)
    return ZONE_TYPE_TEMP;
  if (strcmp(type_str, "water") == 0)
    return ZONE_TYPE_WATER;

  return ZONE_TYPE_UNKNOWN;
}

const char *zone_type_to_string(zone_type_t type) {
  switch (type) {
  case ZONE_TYPE_DOOR:
    return "door";
  case ZONE_TYPE_WINDOW:
    return "window";
  case ZONE_TYPE_MOTION:
    return "motion";
  case ZONE_TYPE_CAMERA:
    return "camera";
  case ZONE_TYPE_GLASS_BREAK:
    return "glass_break";
  case ZONE_TYPE_FIRE:
    return "fire";
  case ZONE_TYPE_SMOKE:
    return "smoke";
  case ZONE_TYPE_PANIC:
    return "panic";
  case ZONE_TYPE_MEDICAL:
    return "medical";
  case ZONE_TYPE_TEMP:
    return "temp";
  case ZONE_TYPE_WATER:
    return "water";
  default:
    return "unknown";
  }
}

zone_call_t zone_call_from_string(const char *type_str) {
  if (type_str == NULL)
    return ZONE_CALL_UNKNOWN;
  if (strcmp(type_str, "fire") == 0)
    return ZONE_CALL_FIRE;
  if (strcmp(type_str, "police") == 0)
    return ZONE_CALL_POLICE;
  if (strcmp(type_str, "medical") == 0)
    return ZONE_CALL_MEDICAL;
  if (strcmp(type_str, "other") == 0)
    return ZONE_CALL_OTHER;
  return ZONE_CALL_UNKNOWN;
}

const char *zone_call_to_string(zone_call_t type) {
  switch (type) {
  case ZONE_CALL_FIRE:
    return "fire";
  case ZONE_CALL_POLICE:
    return "police";
  case ZONE_CALL_MEDICAL:
    return "medical";
  case ZONE_CALL_OTHER:
    return "other";
  default:
    return "unknown";
  }
}

relay_type_t relay_type_from_string(const char *type_str) {
  if (type_str == NULL)
    return RELAY_TYPE_UNKNOWN;

  if (strcmp(type_str, "alarm") == 0)
    return RELAY_TYPE_ALARM;
  if (strcmp(type_str, "siren") == 0)
    return RELAY_TYPE_SIREN;
  if (strcmp(type_str, "strobe") == 0)
    return RELAY_TYPE_STROBE;
  if (strcmp(type_str, "chime") == 0)
    return RELAY_TYPE_CHIME;
  if (strcmp(type_str, "bell") == 0)
    return RELAY_TYPE_BELL;
  if (strcmp(type_str, "light") == 0)
    return RELAY_TYPE_LIGHT;
  if (strcmp(type_str, "door_lock") == 0)
    return RELAY_TYPE_DOOR_LOCK;
  if (strcmp(type_str, "gate") == 0)
    return RELAY_TYPE_GATE;

  return RELAY_TYPE_UNKNOWN;
}

const char *relay_type_to_string(relay_type_t type) {
  switch (type) {
  case RELAY_TYPE_ALARM:
    return "alarm";
  case RELAY_TYPE_SIREN:
    return "siren";
  case RELAY_TYPE_STROBE:
    return "strobe";
  case RELAY_TYPE_CHIME:
    return "chime";
  case RELAY_TYPE_BELL:
    return "bell";
  case RELAY_TYPE_LIGHT:
    return "light";
  case RELAY_TYPE_DOOR_LOCK:
    return "door_lock";
  case RELAY_TYPE_GATE:
    return "gate";
  default:
    return "unknown";
  }
}

static char *read_file(const char *filename) {
  char path[128];
  get_full_path(path, filename);

#ifndef ESP_PLATFORM
  printf("[STORAGE] Trying to open: %s\n", path);
#endif

  FILE *f = fopen(path, "rb");
  if (!f) {
#ifdef ESP_PLATFORM
    ESP_LOGW(TAG, "File not found: %s", path);
#else
    printf("[STORAGE] File not found: %s (errno: %d)\n", path, errno);
#endif
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
#ifdef ESP_PLATFORM
  char *data = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  char *data = malloc(len + 1);
#endif
  if (data) {
    fread(data, 1, len, f);
    data[len] = '\0';
  }
  fclose(f);
  return data;
}

#ifdef ESP_PLATFORM

sdmmc_card_t *card;
const char mount_point[] = "/sdcard";

esp_err_t storage_mount_sd(void) {
  esp_err_t ret;
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  // Use SPI2 (FSPI) as it's the standard for general purpose SPI on S3
  host.slot = SPI2_HOST;

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = SD_MOSI,
      .miso_io_num = SD_MISO,
      .sclk_io_num = SD_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  // Initialize the SPI bus if it hasn't been initialized by Ethernet yet
  ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    return ret;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = SD_CS;
  slot_config.host_id = host.slot;

  ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config,
                                &card);
  return ret;
}

void storage_unmount_sd(void) {
  esp_vfs_fat_sdcard_unmount(mount_point, card);
  ESP_LOGI("STORAGE", "SD Card unmounted");
}

esp_err_t init_spiffs(void) {
  ESP_LOGI("STORAGE", "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label =
                                    "storage", // Matches your partitions.csv
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE("STORAGE", "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE("STORAGE", "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE("STORAGE", "Failed to initialize SPIFFS (%s)",
               esp_err_to_name(ret));
    }
    return ret;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE("STORAGE", "Failed to get partition info (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI("STORAGE", "Partition size: total: %d, used: %d", total, used);
  }
  return ESP_OK;
}

void storage_sync_sd_to_internal(void) {
  const char *targets[] = {"index.html",  "users.html",  "audit.html",
                           "tester.html", "config.json", "network.json",
                           "zones.json",  "relays.json", "esphome.json"};
  int num_targets = sizeof(targets) / sizeof(targets[0]);
  for (int i = 0; i < num_targets; i++) {
    char src[64], dst[64];
    snprintf(src, sizeof(src), "/sdcard/%s", targets[i]);
    snprintf(dst, sizeof(dst), "/spiffs/%s", targets[i]);

    FILE *f_src = fopen(src, "rb");
    if (f_src == NULL)
      continue;

    FILE *f_dst = fopen(dst, "wb");
    if (f_dst) {
      char buf[512];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), f_src)) > 0)
        fwrite(buf, 1, n, f_dst);
      fclose(f_dst);
      ESP_LOGI("STORAGE", "Updated %s from SD", targets[i]);
    }
    fclose(f_src);
  }
}
#endif

#ifdef ESP_PLATFORM
static void *psram_malloc(size_t sz) {
  // Allocate from external PSRAM, fallback to internal if full
  return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#endif

// --- INITIALIZATION ---

void storage_init(void) {
#ifdef ESP_PLATFORM
  if (storage_mutex == NULL) {
    storage_mutex = xSemaphoreCreateRecursiveMutex();
  }

  // Force cJSON to use PSRAM for all JSON parsing and generation
  cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = heap_caps_free};
  cJSON_InitHooks(&hooks);

  ESP_LOGI(TAG, "Mounting SPIFFS Partition...");
  esp_vfs_spiffs_conf_t conf = {.base_path = BASE_PATH,
                                .partition_label = NULL,
                                .max_files = 15,
                                .format_if_mount_failed = true};
  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS Mount Failed (%s)", esp_err_to_name(ret));
  } else {
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS Mounted: %d/%d KB used", (int)used / 1024,
             (int)total / 1024);
  }

  // Initialize HTTPS certificate manager
  ESP_LOGI(TAG, "Initializing certificate manager...");
  cert_mgr_init();

#else
  printf("[STORAGE] Linux Mock Filesystem Initialized: %s\n", BASE_PATH);
#endif
}

// Helper to read a file into a dynamic string buffer
char *read_file_to_string(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (f == NULL)
    return NULL;

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);

#ifdef ESP_PLATFORM
  char *buffer =
      heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  char *buffer = malloc(length + 1);
#endif
  if (buffer) {
    fread(buffer, 1, length, f);
    buffer[length] = '\0';
  }
  fclose(f);
  return buffer;
}

// Helper to write a string buffer to a file
void write_string_to_file(const char *filename, const char *content) {
  FILE *f = fopen(filename, "wb");
  if (f == NULL)
    return;
  fputs(content, f);
  fclose(f);
}

void storage_add_rf_user(const char *name, const char *rf_code) {
  storage_lock();
  // 1. Load existing users.json
  char *json_str = read_file_to_string("/spiffs/users.json");
  cJSON *root = cJSON_Parse(json_str);

  if (!root)
    root = cJSON_CreateArray(); // Create new if file doesn't exist

  // 2. Create the new user object
  cJSON *newUser = cJSON_CreateObject();
  cJSON_AddStringToObject(newUser, "name", name);
  cJSON_AddStringToObject(newUser, "rf", rf_code);
  cJSON_AddNumberToObject(newUser, "level", 1); // Default access level

  // 3. Add to array and save
  cJSON_AddItemToArray(root, newUser);
  char *rendered = cJSON_Print(root);
  if (rendered) {
    write_string_to_file("/spiffs/users.json", rendered);
  }

  // 4. Cleanup
  cJSON_Delete(root);
  free(json_str);
  free(rendered);
  storage_unlock();
}
// --- PERSISTENCE ---

void storage_save_users(void) {
  storage_lock();
  cJSON *root = cJSON_CreateArray();
  if (!root) {
    storage_unlock();
    return;
  }
  for (int i = 0; i < u_count; i++) {
    cJSON *item = cJSON_CreateObject();
    if (!item)
      continue;
    cJSON_AddStringToObject(item, "name", users[i].name);
    // Save plaintext PIN
    if (users[i].pin[0] != '\0') {
      cJSON_AddStringToObject(item, "pin", users[i].pin);
    }
    // Emergency PIN for Noonlight (separate from login PIN)
    if (users[i].emergency_pin[0] != '\0') {
      cJSON_AddStringToObject(item, "emergency_pin", users[i].emergency_pin);
    }
    cJSON_AddStringToObject(item, "phone", users[i].phone);
    cJSON_AddStringToObject(item, "email", users[i].email);
    cJSON_AddNumberToObject(item, "notify", users[i].notify);
    cJSON_AddBoolToObject(item, "is_admin", users[i].is_admin);
    cJSON_AddBoolToObject(item, "requires_2fa", users[i].requires_2fa);
    if (users[i].totp_secret[0] != '\0') {
      cJSON_AddStringToObject(item, "totp_secret", users[i].totp_secret);
    }
    cJSON_AddItemToArray(root, item);
  }
  char *json_str = cJSON_Print(root);
  if (json_str) {
    char path[128];
    get_full_path(path, "users.json");
    FILE *f = fopen(path, "w");
    if (f) {
      fputs(json_str, f);
      fclose(f);
    }
    free(json_str);
  }
  cJSON_Delete(root);
  storage_unlock();
}

void storage_save_config(void) {
  storage_lock();
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    storage_unlock();
    return;
  }

  cJSON_AddStringToObject(root, "account_id", config.account_id);
  cJSON_AddStringToObject(root, "pin", config.pin);
  cJSON_AddStringToObject(root, "name", config.name);
  cJSON_AddStringToObject(root, "address1", config.address1);
  cJSON_AddStringToObject(root, "address2", config.address2);
  cJSON_AddStringToObject(root, "city", config.city);
  cJSON_AddStringToObject(root, "state", config.state);
  cJSON_AddStringToObject(root, "zip_code", config.zip_code);
  cJSON_AddStringToObject(root, "email", config.email);
  cJSON_AddStringToObject(root, "phone", config.phone);
  cJSON_AddStringToObject(root, "instructions", config.instructions);
  cJSON_AddNumberToObject(root, "latitude", config.latitude);
  cJSON_AddNumberToObject(root, "longitude", config.longitude);
  cJSON_AddNumberToObject(root, "accuracy", config.accuracy);

  cJSON_AddStringToObject(root, "monitor_service_id",
                          config.monitor_service_id);
  cJSON_AddStringToObject(root, "monitor_service_key",
                          config.monitor_service_key);
  cJSON_AddStringToObject(root, "monitoring_url", config.monitoring_url);
  cJSON_AddNumberToObject(root, "notify", config.notify);

  cJSON *nl_array = cJSON_CreateArray();
  if (nl_array) {
    for (int i = 0; i < MAX_ALARM_SLOTS; i++) {
      cJSON *str_item = cJSON_CreateString(config.nl_ids[i]);
      if (str_item)
        cJSON_AddItemToArray(nl_array, str_item);
    }
    cJSON_AddItemToObject(root, "nl_ids", nl_array);
  }

  cJSON_AddBoolToObject(root, "is_monitor_fire", config.is_monitor_fire);
  cJSON_AddBoolToObject(root, "is_monitor_police", config.is_monitor_police);
  cJSON_AddBoolToObject(root, "is_monitor_medical", config.is_monitor_medical);
  cJSON_AddBoolToObject(root, "is_monitor_other", config.is_monitor_other);

  cJSON_AddStringToObject(root, "smtp_server", config.smtp_server);
  cJSON_AddNumberToObject(root, "smtp_port", config.smtp_port);
  cJSON_AddStringToObject(root, "smtp_user", config.smtp_user);
  cJSON_AddStringToObject(root, "smtp_pass", config.smtp_pass);
  cJSON_AddStringToObject(root, "mqtt_server", config.mqtt_server);
  cJSON_AddNumberToObject(root, "mqtt_port", config.mqtt_port);
  cJSON_AddStringToObject(root, "mqtt_user", config.mqtt_user);
  cJSON_AddStringToObject(root, "mqtt_pass", config.mqtt_pass);
  cJSON_AddStringToObject(root, "telegram_id", config.telegram_id);
  cJSON_AddStringToObject(root, "telegram_token", config.telegram_token);
  cJSON_AddBoolToObject(root, "is_telegram_enabled",
                        config.is_telegram_enabled);
  cJSON_AddStringToObject(root, "nvrserver_url", config.nvrserver_url);
  cJSON_AddStringToObject(root, "haintegration_url", config.haintegration_url);

  cJSON_AddNumberToObject(root, "entry_delay", config.entry_delay);
  cJSON_AddNumberToObject(root, "exit_delay", config.exit_delay);
  cJSON_AddNumberToObject(root, "cancel_delay", config.cancel_delay);

  cJSON_AddNumberToObject(root, "watchdog_esphome_sec",
                          config.watchdog_esphome_sec);
  cJSON_AddNumberToObject(root, "watchdog_camera_fails",
                          config.watchdog_camera_fails);
  cJSON_AddNumberToObject(root, "watchdog_network_sec",
                          config.watchdog_network_sec);

  char *rendered = cJSON_Print(root);
  if (rendered) {
    char path[128];
    get_full_path(path, "config.json");
    FILE *f = fopen(path, "w");
    if (f) {
      fputs(rendered, f);
      fclose(f);
    }
    free(rendered);
  }
  cJSON_Delete(root);
  storage_unlock();
}

// --- LOADING ---

void storage_load_all() {
  storage_lock();
  char *data;
  cJSON *root, *item;

  // 1. Load Config
  if ((data = read_file("config.json"))) {
    printf("[STORAGE] config.json read successfully (%zu bytes)\n",
           strlen(data));
    root = cJSON_Parse(data);
    if (!root) {
      printf("[STORAGE] ERROR: Failed to parse config.json\n");
    }
    if (root) {
      safe_strncpy(config.account_id, cJSON_GetObjectItem(root, "account_id"),
                   STR_SMALL);
      safe_strncpy(config.pin, cJSON_GetObjectItem(root, "pin"), STR_SMALL);
      safe_strncpy(config.name, cJSON_GetObjectItem(root, "name"), STR_MEDIUM);
      safe_strncpy(config.address1, cJSON_GetObjectItem(root, "address1"),
                   STR_MEDIUM);
      safe_strncpy(config.address2, cJSON_GetObjectItem(root, "address2"),
                   STR_MEDIUM);
      safe_strncpy(config.city, cJSON_GetObjectItem(root, "city"), STR_SMALL);
      safe_strncpy(config.state, cJSON_GetObjectItem(root, "state"), STR_SMALL);
      safe_strncpy(config.zip_code, cJSON_GetObjectItem(root, "zip_code"),
                   STR_SMALL);
      safe_strncpy(config.email, cJSON_GetObjectItem(root, "email"),
                   STR_MEDIUM);
      safe_strncpy(config.phone, cJSON_GetObjectItem(root, "phone"), STR_SMALL);
      safe_strncpy(config.instructions,
                   cJSON_GetObjectItem(root, "instructions"), STR_LARGE);

      cJSON *lat = cJSON_GetObjectItem(root, "latitude");
      config.latitude = lat ? lat->valuedouble : 0;
      cJSON *lon = cJSON_GetObjectItem(root, "longitude");
      config.longitude = lon ? lon->valuedouble : 0;
      config.accuracy = safe_get_int(cJSON_GetObjectItem(root, "accuracy"), 10);

      safe_strncpy(config.monitor_service_id,
                   cJSON_GetObjectItem(root, "monitor_service_id"), STR_MEDIUM);
      safe_strncpy(config.monitor_service_key,
                   cJSON_GetObjectItem(root, "monitor_service_key"),
                   STR_MEDIUM);
      safe_strncpy(config.monitoring_url,
                   cJSON_GetObjectItem(root, "monitoring_url"), STR_MEDIUM);
      config.notify = safe_get_int(cJSON_GetObjectItem(root, "notify"), 0);

      cJSON *nl_ids = cJSON_GetObjectItem(root, "nl_ids");
      if (cJSON_IsArray(nl_ids)) {
        for (int i = 0; i < cJSON_GetArraySize(nl_ids) && i < MAX_ALARM_SLOTS;
             i++) {
          cJSON *id_obj = cJSON_GetArrayItem(nl_ids, i);
          if (id_obj && id_obj->valuestring) {
            strncpy(config.nl_ids[i], id_obj->valuestring, 63);
            config.nl_ids[i][63] = '\0';
          }
        }
      }

      config.is_monitor_fire =
          safe_get_bool(cJSON_GetObjectItem(root, "is_monitor_fire"));
      config.is_monitor_police =
          safe_get_bool(cJSON_GetObjectItem(root, "is_monitor_police"));
      config.is_monitor_medical =
          safe_get_bool(cJSON_GetObjectItem(root, "is_monitor_medical"));
      config.is_monitor_other =
          safe_get_bool(cJSON_GetObjectItem(root, "is_monitor_other"));

      safe_strncpy(config.smtp_server, cJSON_GetObjectItem(root, "smtp_server"),
                   STR_MEDIUM);
      config.smtp_port =
          safe_get_int(cJSON_GetObjectItem(root, "smtp_port"), 587);
      safe_strncpy(config.smtp_user, cJSON_GetObjectItem(root, "smtp_user"),
                   STR_MEDIUM);
      safe_strncpy(config.smtp_pass, cJSON_GetObjectItem(root, "smtp_pass"),
                   STR_MEDIUM);

      safe_strncpy(config.mqtt_server, cJSON_GetObjectItem(root, "mqtt_server"),
                   STR_MEDIUM);
      config.mqtt_port =
          safe_get_int(cJSON_GetObjectItem(root, "mqtt_port"), 1883);
      safe_strncpy(config.mqtt_user, cJSON_GetObjectItem(root, "mqtt_user"),
                   STR_SMALL);
      safe_strncpy(config.mqtt_pass, cJSON_GetObjectItem(root, "mqtt_pass"),
                   STR_SMALL);

      safe_strncpy(config.telegram_id, cJSON_GetObjectItem(root, "telegram_id"),
                   STR_SMALL);
      safe_strncpy(config.telegram_token,
                   cJSON_GetObjectItem(root, "telegram_token"), STR_MEDIUM);
      config.is_telegram_enabled =
          safe_get_bool(cJSON_GetObjectItem(root, "is_telegram_enabled"));

      safe_strncpy(config.nvrserver_url,
                   cJSON_GetObjectItem(root, "nvrserver_url"), STR_MEDIUM);
      safe_strncpy(config.haintegration_url,
                   cJSON_GetObjectItem(root, "haintegration_url"), STR_MEDIUM);

      config.entry_delay =
          safe_get_int(cJSON_GetObjectItem(root, "entry_delay"), 30);
      config.exit_delay =
          safe_get_int(cJSON_GetObjectItem(root, "exit_delay"), 60);
      config.cancel_delay =
          safe_get_int(cJSON_GetObjectItem(root, "cancel_delay"), 10);

      config.watchdog_esphome_sec =
          safe_get_int(cJSON_GetObjectItem(root, "watchdog_esphome_sec"), 15);
      config.watchdog_camera_fails =
          safe_get_int(cJSON_GetObjectItem(root, "watchdog_camera_fails"), 3);
      config.watchdog_network_sec =
          safe_get_int(cJSON_GetObjectItem(root, "watchdog_network_sec"), 30);

      // Validate loaded config
      validate_config();

      cJSON_Delete(root);
    }
    free(data);
  }

  // 2. Load Users
  u_count = 0;
  if ((data = read_file("users.json"))) {
    printf("[STORAGE] users.json read successfully (%zu bytes)\n",
           strlen(data));
    root = cJSON_Parse(data);
    if (!root) {
      printf("[STORAGE] ERROR: Failed to parse users.json\n");
    }
    if (root) {
      cJSON_ArrayForEach(item, root) {
        if (u_count >= MAX_USERS)
          break;
        safe_strncpy(users[u_count].name, cJSON_GetObjectItem(item, "name"),
                     STR_SMALL);

        // Load plaintext PIN
        safe_strncpy(users[u_count].pin, cJSON_GetObjectItem(item, "pin"),
                     STR_SMALL);

        // Load emergency PIN for Noonlight (separate from login PIN)
        cJSON *emergency_pin_item = cJSON_GetObjectItem(item, "emergency_pin");
        if (emergency_pin_item && cJSON_IsString(emergency_pin_item)) {
          strncpy(users[u_count].emergency_pin, emergency_pin_item->valuestring,
                  sizeof(users[u_count].emergency_pin) - 1);
          users[u_count]
              .emergency_pin[sizeof(users[u_count].emergency_pin) - 1] = '\0';
        } else {
          users[u_count].emergency_pin[0] = '\0';
        }

        safe_strncpy(users[u_count].phone, cJSON_GetObjectItem(item, "phone"),
                     STR_SMALL);
        safe_strncpy(users[u_count].email, cJSON_GetObjectItem(item, "email"),
                     STR_MEDIUM);
        users[u_count].notify =
            safe_get_int(cJSON_GetObjectItem(item, "notify"), 0);
        users[u_count].is_admin =
            safe_get_bool(cJSON_GetObjectItem(item, "is_admin"));
        users[u_count].requires_2fa =
            safe_get_bool(cJSON_GetObjectItem(item, "requires_2fa"));
        // Load TOTP secret if present
        cJSON *totp_secret_item = cJSON_GetObjectItem(item, "totp_secret");
        if (totp_secret_item && cJSON_IsString(totp_secret_item)) {
          safe_strncpy(users[u_count].totp_secret, totp_secret_item,
                       STR_MEDIUM);
        } else {
          users[u_count].totp_secret[0] = '\0'; // No secret set
        }
        u_count++;
      }
      cJSON_Delete(root);
    }
    free(data);
  }

  // If no users exist, create Installer user with master PIN from config
  if (u_count == 0 && config.pin[0] != '\0') {
    printf(
        "[STORAGE] No users found - Creating Installer user with master PIN\n");
    strncpy(users[0].name, "Installer", STR_SMALL - 1);
    users[0].name[STR_SMALL - 1] = '\0';

    // Copy master PIN in plaintext
    strncpy(users[0].pin, config.pin, STR_SMALL - 1);
    users[0].pin[STR_SMALL - 1] = '\0';

    strncpy(users[0].phone, "555-0000", STR_SMALL - 1);
    strncpy(users[0].email, config.email, STR_MEDIUM - 1);
    users[0].notify = 0;              // No notifications for installer
    users[0].is_admin = true;         // Installer has admin rights
    users[0].requires_2fa = false;    // Optional 2FA for installer
    users[0].totp_secret[0] = '\0';   // No TOTP secret yet
    users[0].emergency_pin[0] = '\0'; // No emergency PIN for installer
    users[0].pin[0] = '\0';           // Clear legacy plaintext

    u_count = 1;
    storage_save_users(); // Persist the new user
    printf("[STORAGE] Installer user created and persisted to users.json\n");
  }

  // 3. Load Relays
  r_count = 0;
  if ((data = read_file("relays.json"))) {
    root = cJSON_Parse(data);
    if (root) {
      cJSON_ArrayForEach(item, root) {
        if (r_count >= MAX_RELAYS)
          break;
        relays[r_count].id =
            safe_get_int(cJSON_GetObjectItem(item, "id"), r_count);
        safe_strncpy(relays[r_count].name, cJSON_GetObjectItem(item, "name"),
                     STR_MEDIUM);
        safe_strncpy(relays[r_count].description,
                     cJSON_GetObjectItem(item, "description"), STR_MEDIUM);
        relays[r_count].duration =
            safe_get_int(cJSON_GetObjectItem(item, "duration"), 0);
        safe_strncpy(relays[r_count].location,
                     cJSON_GetObjectItem(item, "location"), STR_MEDIUM);
        safe_strncpy(relays[r_count].type, cJSON_GetObjectItem(item, "type"),
                     STR_SMALL);
        relays[r_count].is_repeat =
            safe_get_bool(cJSON_GetObjectItem(item, "is_repeat"));
        relays[r_count].gpio =
            safe_get_int(cJSON_GetObjectItem(item, "gpio"), 0);
        r_count++;
      }
      cJSON_Delete(root);
    }
    free(data);
  }

  // 4. Load Zones
  z_count = 0;
  if ((data = read_file("zones.json"))) {
    root = cJSON_Parse(data);
    if (root) {
      cJSON_ArrayForEach(item, root) {
        if (z_count >= MAX_ZONES)
          break;
        zones[z_count].id =
            safe_get_int(cJSON_GetObjectItem(item, "id"), z_count);
        safe_strncpy(zones[z_count].name, cJSON_GetObjectItem(item, "name"),
                     STR_SMALL);
        safe_strncpy(zones[z_count].description,
                     cJSON_GetObjectItem(item, "description"), STR_MEDIUM);
        safe_strncpy(zones[z_count].type, cJSON_GetObjectItem(item, "type"),
                     STR_SMALL);
        safe_strncpy(zones[z_count].call, cJSON_GetObjectItem(item, "call"),
                     STR_SMALL);
        safe_strncpy(zones[z_count].location,
                     cJSON_GetObjectItem(item, "location"), STR_SMALL);
        safe_strncpy(zones[z_count].model, cJSON_GetObjectItem(item, "model"),
                     STR_SMALL);
        safe_strncpy(zones[z_count].manufacturer,
                     cJSON_GetObjectItem(item, "manufacturer"), STR_SMALL);
        zones[z_count].is_chime =
            safe_get_bool(cJSON_GetObjectItem(item, "is_chime"));
        zones[z_count].is_alarm_on_armed_only =
            safe_get_bool(cJSON_GetObjectItem(item, "is_alarm_on_armed_only"));
        zones[z_count].gpio =
            safe_get_int(cJSON_GetObjectItem(item, "gpio"), 0);
        zones[z_count].is_i2c =
            safe_get_bool(cJSON_GetObjectItem(item, "is_i2c"));
        zones[z_count].i2c_address =
            safe_get_int(cJSON_GetObjectItem(item, "i2c_address"), 0);
        zones[z_count].is_perimeter =
            safe_get_bool(cJSON_GetObjectItem(item, "is_perimeter"));
        zones[z_count].is_interior =
            safe_get_bool(cJSON_GetObjectItem(item, "is_interior"));
        zones[z_count].is_panic =
            safe_get_bool(cJSON_GetObjectItem(item, "is_panic"));
        zones[z_count].is_alert_sent = false;
        z_count++;
      }
      cJSON_Delete(root);
    }
    free(data);
  }

  // 5 & 6. Load ESPHome Devices and Generate Virtual Zones/Relays
  esphome_count = 0;
  if ((data = read_file("esphome.json"))) {
    root = cJSON_Parse(data);
    if (root) {
      cJSON_ArrayForEach(item, root) {
        if (esphome_count >= 32)
          break;

        esphome_device_t *dev = &esphome_devices[esphome_count];

        safe_strncpy(esphome_devices[esphome_count].hostname,
                     cJSON_GetObjectItem(item, "hostname"), STR_SMALL);
        esphome_devices[esphome_count].port =
            safe_get_int(cJSON_GetObjectItem(item, "port"), 6053);
        safe_strncpy(esphome_devices[esphome_count].friendly_name,
                     cJSON_GetObjectItem(item, "friendly_name"), STR_SMALL);
        safe_strncpy(esphome_devices[esphome_count].password,
                     cJSON_GetObjectItem(item, "password"), STR_SMALL);
        safe_strncpy(esphome_devices[esphome_count].encryption_key,
                     cJSON_GetObjectItem(item, "encryption_key"), STR_MEDIUM);
        esphome_devices[esphome_count].enabled =
            safe_get_bool(cJSON_GetObjectItem(item, "enabled"));

        // Parse nested zone object
        cJSON *zone_obj = cJSON_GetObjectItem(item, "zone");
        if (zone_obj) {
          esphome_devices[esphome_count].virtual_zone_start =
              safe_get_int(cJSON_GetObjectItem(zone_obj, "id"), 0);
          safe_strncpy(esphome_devices[esphome_count].description,
                       cJSON_GetObjectItem(zone_obj, "description"),
                       STR_MEDIUM);
          safe_strncpy(esphome_devices[esphome_count].location,
                       cJSON_GetObjectItem(zone_obj, "location"), STR_MEDIUM);

          // 6a. Create virtual zone if configured and enabled
          if (dev->virtual_zone_start > 0 && dev->enabled &&
              z_count < MAX_ZONES) {
            zone_t *vzone = &zones[z_count];
            vzone->id = dev->virtual_zone_start;
            snprintf(vzone->name, STR_SMALL, "%s", dev->friendly_name);
            safe_strncpy(vzone->description,
                         cJSON_GetObjectItem(zone_obj, "description"),
                         STR_MEDIUM);
            safe_strncpy(vzone->type, cJSON_GetObjectItem(zone_obj, "type"),
                         STR_SMALL);
            safe_strncpy(vzone->call, cJSON_GetObjectItem(zone_obj, "call"),
                         STR_SMALL);
            safe_strncpy(vzone->location,
                         cJSON_GetObjectItem(zone_obj, "location"), STR_SMALL);
            safe_strncpy(vzone->model, cJSON_GetObjectItem(zone_obj, "model"),
                         STR_SMALL);
            safe_strncpy(vzone->manufacturer,
                         cJSON_GetObjectItem(zone_obj, "manufacturer"),
                         STR_SMALL);
            vzone->is_chime =
                safe_get_bool(cJSON_GetObjectItem(zone_obj, "is_chime"));
            vzone->is_alarm_on_armed_only = safe_get_bool(
                cJSON_GetObjectItem(zone_obj, "is_alarm_on_armed_only"));
            vzone->gpio = -1; // Virtual - no GPIO
            vzone->is_i2c = false;
            vzone->i2c_address = 0;
            vzone->is_perimeter =
                safe_get_bool(cJSON_GetObjectItem(zone_obj, "is_perimeter"));
            vzone->is_interior =
                safe_get_bool(cJSON_GetObjectItem(zone_obj, "is_interior"));
            vzone->is_panic =
                safe_get_bool(cJSON_GetObjectItem(zone_obj, "is_panic"));
            vzone->is_alert_sent = false;
            z_count++;
          }
        } else {
          // Fallback to old format
          esphome_devices[esphome_count].virtual_zone_start =
              safe_get_int(cJSON_GetObjectItem(item, "virtual_zone_start"), 0);
          safe_strncpy(esphome_devices[esphome_count].description,
                       cJSON_GetObjectItem(item, "description"), STR_MEDIUM);
          safe_strncpy(esphome_devices[esphome_count].location,
                       cJSON_GetObjectItem(item, "location"), STR_MEDIUM);
        }

        // Parse nested relay object
        cJSON *relay_obj = cJSON_GetObjectItem(item, "relay");
        if (relay_obj) {
          esphome_devices[esphome_count].virtual_relay_start =
              safe_get_int(cJSON_GetObjectItem(relay_obj, "id"), 0);
          esphome_devices[esphome_count].entity_key =
              safe_get_uint32(cJSON_GetObjectItem(relay_obj, "entity_key"), 0);
          printf("[STORAGE] ESPHome device %d: relay_id=%d, entity_key=%" PRIu32
                 "\n",
                 esphome_count,
                 esphome_devices[esphome_count].virtual_relay_start,
                 esphome_devices[esphome_count].entity_key);

          // 6b. Create virtual relay if configured and enabled
          if (dev->virtual_relay_start > 0 && dev->enabled &&
              r_count < MAX_RELAYS) {
            relay_t *vrelay = &relays[r_count];
            vrelay->id = dev->virtual_relay_start;
            snprintf(vrelay->name, STR_MEDIUM, "%s", dev->friendly_name);
            safe_strncpy(vrelay->description,
                         cJSON_GetObjectItem(relay_obj, "description"),
                         STR_MEDIUM);
            vrelay->duration =
                safe_get_int(cJSON_GetObjectItem(relay_obj, "duration"), 0);
            safe_strncpy(vrelay->location,
                         cJSON_GetObjectItem(relay_obj, "location"),
                         STR_MEDIUM);
            safe_strncpy(vrelay->type, cJSON_GetObjectItem(relay_obj, "type"),
                         STR_SMALL);
            vrelay->is_repeat =
                safe_get_bool(cJSON_GetObjectItem(relay_obj, "is_repeat"));
            vrelay->gpio = -1; // Virtual - no GPIO
            r_count++;
          }
        } else {
          // Fallback to old format
          esphome_devices[esphome_count].virtual_relay_start =
              safe_get_int(cJSON_GetObjectItem(item, "virtual_relay_start"), 0);
          esphome_devices[esphome_count].entity_key =
              0; // Default for old format
        }

        esphome_count++; // Load ALL devices (enabled and disabled)
      }
      cJSON_Delete(root);
    }
    free(data);
  }

  storage_load_network();
  storage_unlock();
}

// storage_mgr.c - This part is universal
void storage_load_network(void) {
  storage_lock();
  char *json_str = read_file("network.json");
  if (!json_str) {
    net_cfg.use_dhcp = true; // Fallback to DHCP
    storage_unlock();
    return;
  }

  cJSON *root = cJSON_Parse(json_str);
  if (root) {
    net_cfg.use_dhcp = safe_get_bool(cJSON_GetObjectItem(root, "use_dhcp"));
    safe_strncpy(net_cfg.ip, cJSON_GetObjectItem(root, "ip"),
                 sizeof(net_cfg.ip));
    safe_strncpy(net_cfg.netmask, cJSON_GetObjectItem(root, "netmask"),
                 sizeof(net_cfg.netmask));
    safe_strncpy(net_cfg.gateway, cJSON_GetObjectItem(root, "gateway"),
                 sizeof(net_cfg.gateway));
    safe_strncpy(net_cfg.dns, cJSON_GetObjectItem(root, "dns"),
                 sizeof(net_cfg.dns));

    cJSON_Delete(root);
  }
  if (json_str)
    free(json_str);
  storage_unlock();
}

void storage_save_network(void) {
  storage_lock();
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    storage_unlock();
    return;
  }
  cJSON_AddBoolToObject(root, "use_dhcp", net_cfg.use_dhcp);
  cJSON_AddStringToObject(root, "ip", net_cfg.ip);
  cJSON_AddStringToObject(root, "netmask", net_cfg.netmask);
  cJSON_AddStringToObject(root, "gateway", net_cfg.gateway);
  cJSON_AddStringToObject(root, "dns", net_cfg.dns);

  char *rendered = cJSON_Print(root);
  if (rendered) {
    char path[128];
    get_full_path(path, "network.json");
    write_string_to_file(path, rendered);
    free(rendered);
  }
  cJSON_Delete(root);
  storage_unlock();
}

void storage_debug_print() {
  storage_lock();
  printf("\n================= SYSTEM CONFIG DEBUG =================\n");
  printf("Account ID:    [%s]\n", config.account_id);
  printf("Owner Name:    [%s]\n", config.name);
  printf("Master PIN:    [%s]\n", config.pin);
  printf("Address:       %s, %s, %s %s\n", config.address1, config.city,
         config.state, config.zip_code);
  printf("Location:      Lat %.4f, Lon %.4f\n", config.latitude,
         config.longitude);
  printf("Notify:        [%d]\n", config.notify);
  printf("Delays:        Exit %ds | Entry %ds | Cancel %ds\n",
         config.exit_delay, config.entry_delay, config.cancel_delay);

  printf("\n--- NOTIFICATIONS ---\n");
  printf("Telegram:      %s | ID: [%s]\n",
         config.is_telegram_enabled ? "ON" : "OFF", config.telegram_id);
  printf("SMTP Server:   %s:%d | User: %s\n", config.smtp_server,
         config.smtp_port, config.smtp_user);
  printf("MQTT Server:   %s:%d\n", config.mqtt_server, config.mqtt_port);

  printf("\n--- AUTHORIZED USERS (%d LOADED) ---\n", u_count);
  if (u_count == 0) {
    printf(" [!] No users found in users.json\n");
  } else {
    printf(" %-15s | %-6s | %-12s |%-17s | %s\n", "NAME", "PIN", "PHONE",
           " NOTIFY",
           "EMAIL"
           "NOTIFY");
    printf(" ------------------------------------------------------------------"
           "-----------------------------\n");
    for (int i = 0; i < u_count; i++) {
      printf(" %-15s | %-6s | %-12s | %-15d  |%s\n", users[i].name,
             users[i].pin, users[i].phone, users[i].notify, users[i].email);
    }
  }

  printf("\n--- ZONES (%d LOADED) ---\n", z_count);
  for (int i = 0; i < z_count; i++) {
    printf("ID %d: %-12s | GPIO: %-2d | Type: %-8s | Chime: %s | Perim: %s | "
           "I2C: %s (0x%X)\n",
           zones[i].id, zones[i].name, zones[i].gpio, zones[i].type,
           zones[i].is_chime ? "YES" : "NO",
           zones[i].is_perimeter ? "YES" : "NO", zones[i].is_i2c ? "YES" : "NO",
           zones[i].i2c_address);
  }

  printf("\n--- RELAYS (%d LOADED) ---\n", r_count);
  for (int i = 0; i < r_count; i++) {
    printf("Relay %d: %-12s | GPIO: %-2d | Type: %-8s | Repeat: %s\n",
           relays[i].id, relays[i].name, relays[i].gpio, relays[i].type,
           relays[i].is_repeat ? "YES" : "NO");
  }
  printf("=======================================================\n\n");
  storage_unlock();
}

// ========== ESPHome Device Management Functions ==========

char *esphome_devices_to_json(void) {
  storage_lock();
  cJSON *root = cJSON_CreateArray();
  if (!root) {
    storage_unlock();
    return NULL;
  }

  for (int i = 0; i < esphome_count; i++) {
    cJSON *dev = cJSON_CreateObject();
    if (!dev) {
      cJSON_Delete(root);
      storage_unlock();
      return NULL;
    }
    cJSON_AddStringToObject(dev, "hostname", esphome_devices[i].hostname);
    cJSON_AddNumberToObject(dev, "port", esphome_devices[i].port);
    cJSON_AddStringToObject(dev, "friendly_name",
                            esphome_devices[i].friendly_name);
    cJSON_AddStringToObject(dev, "password", esphome_devices[i].password);
    cJSON_AddStringToObject(dev, "encryption_key",
                            esphome_devices[i].encryption_key);
    cJSON_AddNumberToObject(dev, "virtual_zone_start",
                            esphome_devices[i].virtual_zone_start);
    cJSON_AddNumberToObject(dev, "virtual_relay_start",
                            esphome_devices[i].virtual_relay_start);
    cJSON_AddBoolToObject(dev, "enabled", esphome_devices[i].enabled);
    cJSON_AddItemToArray(root, dev);
  }

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  storage_unlock();
  return json_str;
}

int esphome_device_add(const char *hostname, uint16_t port,
                       const char *friendly_name, const char *password,
                       const char *encryption_key, int8_t virtual_zone_start,
                       int8_t virtual_relay_start, bool enabled) {
  if (!hostname || !friendly_name) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG,
             "Invalid parameters: hostname and friendly_name are required");
#endif
    return -1;
  }

  if (esphome_count >= 32) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG, "ESPHome device limit reached (32)");
#endif
    return -1;
  }

  storage_lock();
  esphome_device_t *dev = &esphome_devices[esphome_count];
  strncpy(dev->hostname, hostname, STR_SMALL - 1);
  dev->hostname[STR_SMALL - 1] = '\0';
  dev->port = port;
  strncpy(dev->friendly_name, friendly_name, STR_SMALL - 1);
  dev->friendly_name[STR_SMALL - 1] = '\0';
  strncpy(dev->password, password ? password : "", STR_SMALL - 1);
  dev->password[STR_SMALL - 1] = '\0';
  strncpy(dev->encryption_key, encryption_key ? encryption_key : "",
          STR_MEDIUM - 1);
  dev->encryption_key[STR_MEDIUM - 1] = '\0';
  dev->virtual_zone_start = virtual_zone_start;
  dev->virtual_relay_start = virtual_relay_start;
  dev->enabled = enabled;

  esphome_count++;
  int ret = esphome_devices_save();
  storage_unlock();
  return ret;
}

int esphome_device_update(const char *hostname, uint16_t port,
                          const char *friendly_name, const char *password,
                          const char *encryption_key, int8_t virtual_zone_start,
                          int8_t virtual_relay_start, bool enabled) {
  if (!hostname) {
    return -1;
  }

  storage_lock();
  for (int i = 0; i < esphome_count; i++) {
    if (strcmp(esphome_devices[i].hostname, hostname) == 0) {
      if (port > 0)
        esphome_devices[i].port = port;
      if (friendly_name) {
        strncpy(esphome_devices[i].friendly_name, friendly_name, STR_SMALL - 1);
        esphome_devices[i].friendly_name[STR_SMALL - 1] = '\0';
      }
      if (password) {
        strncpy(esphome_devices[i].password, password, STR_SMALL - 1);
        esphome_devices[i].password[STR_SMALL - 1] = '\0';
      }
      if (encryption_key) {
        strncpy(esphome_devices[i].encryption_key, encryption_key,
                STR_MEDIUM - 1);
        esphome_devices[i].encryption_key[STR_MEDIUM - 1] = '\0';
      }
      if (virtual_zone_start >= 0)
        esphome_devices[i].virtual_zone_start = virtual_zone_start;
      if (virtual_relay_start >= 0)
        esphome_devices[i].virtual_relay_start = virtual_relay_start;
      esphome_devices[i].enabled = enabled;
      int ret = esphome_devices_save();
      storage_unlock();
      return ret;
    }
  }
  storage_unlock();
  return -1; // Not found
}

int esphome_device_delete(const char *hostname) {
  if (!hostname) {
    return -1;
  }

  storage_lock();
  for (int i = 0; i < esphome_count; i++) {
    if (strcmp(esphome_devices[i].hostname, hostname) == 0) {
      // Shift remaining devices down
      for (int j = i; j < esphome_count - 1; j++) {
        esphome_devices[j] = esphome_devices[j + 1];
      }
      esphome_count--;
      memset(&esphome_devices[esphome_count], 0, sizeof(esphome_device_t));
      int ret = esphome_devices_save();
      storage_unlock();
      return ret;
    }
  }
  storage_unlock();
  return -1; // Not found
}

int esphome_devices_save(void) {
  char *json_str = esphome_devices_to_json();
  if (!json_str)
    return -1;

  char path[128];
  get_full_path(path, "esphome.json");
  FILE *f = fopen(path, "w");
  if (f) {
    fputs(json_str, f);
    fclose(f);
  }
  free(json_str);

#ifdef ESP_PLATFORM
  if (f) {
    ESP_LOGI(TAG, "Saved %d ESPHome devices to esphome.json", esphome_count);
  } else {
    ESP_LOGE(TAG, "Failed to save esphome.json");
  }
#endif

  return f ? 0 : -1;
}

// =============================================================================
// Password Hashing and Authentication Functions
// =============================================================================

/**
 * @brief Authenticate user by PIN (supports both legacy and hashed passwords)
 * @param pin PIN to verify
 * @param out_user Pointer to store matched user (can be NULL)
 * @return true if authentication successful, false otherwise
 */
bool user_authenticate_pin(const char *pin, user_t **out_user) {
  if (!pin || strlen(pin) == 0) {
    return false;
  }

  storage_lock();
  for (int i = 0; i < u_count; i++) {
    user_t *user = &users[i];

    // Skip empty users
    if (strlen(user->name) == 0) {
      continue;
    }

    if (strcmp(user->pin, pin) == 0) {
#ifdef ESP_PLATFORM
      ESP_LOGI(TAG, "User '%s' authenticated with plaintext PIN", user->name);
#endif
      if (out_user) {
        *out_user = user;
      }
      storage_unlock();
      return true;
    }
  }

#ifdef ESP_PLATFORM
  ESP_LOGW(TAG, "Authentication failed for PIN");
#endif

  storage_unlock();
  return false;
}

/**
 * @brief Set a new password for a user (stores as hash)
 * @param user_index Index of user in users[] array
 * @param new_pin New PIN to set
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t user_set_password(int user_index, const char *new_pin) {
  if (user_index < 0 || user_index >= u_count) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG, "Invalid user index: %d", user_index);
#endif
    return ESP_FAIL;
  }

  if (!new_pin || strlen(new_pin) < 4) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG, "Invalid PIN (must be at least 4 characters)");
#endif
    return ESP_FAIL;
  }

  storage_lock();
  user_t *user = &users[user_index];

  // Copy plaintext PIN
  strncpy(user->pin, new_pin, STR_SMALL - 1);
  user->pin[STR_SMALL - 1] = '\0';

#ifdef ESP_PLATFORM
  ESP_LOGI(TAG, "Set new password for user '%s'", user->name);
#endif

  storage_unlock();
  return ESP_OK;
}
