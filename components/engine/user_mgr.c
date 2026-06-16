#include "user_mgr.h" // This now pulls in storage_mgr.h and cJSON.h
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "USER_MGR";
#else
#define TAG "USER_MGR"
#endif

// Externs are correct - these live in storage_mgr.c memory
void user_add(const char *name, const char *pin, const char *phone,
              const char *email, int notify, bool is_admin,
              const char *emergency_pin) {
  int count = storage_get_user_count();
  if (count < MAX_USERS) {
    user_t *u = storage_get_user(count);
    memset(u, 0, sizeof(user_t));

    // Using strlcpy (if available) or strncpy safely
    strncpy(u->name, name, STR_SMALL - 1);
    strncpy(u->phone, phone, STR_SMALL - 1);
    strncpy(u->email, email, STR_MEDIUM - 1);
    u->notify = notify;
    u->is_admin = is_admin;

    // Store PIN in plaintext
    if (pin && strlen(pin) > 0) {
      strncpy(u->pin, pin, STR_SMALL - 1);
      u->pin[STR_SMALL - 1] = '\0';
    }

    // Store emergency PIN (plaintext for Noonlight dispatcher verification)
    if (emergency_pin && strlen(emergency_pin) > 0) {
      strncpy(u->emergency_pin, emergency_pin, STR_SMALL - 1);
      u->emergency_pin[STR_SMALL - 1] = '\0';
      printf("[%s] Stored emergency PIN for user: %s\n", TAG, name);
    }

    storage_set_user_count(count + 1);
    storage_save_users();
  }
}

void user_update(const char *name, const char *new_pin, const char *new_phone,
                 const char *new_email, int new_notify, int new_is_admin,
                 const char *new_emergency_pin) {
  for (int i = 0; i < storage_get_user_count(); i++) {
    user_t *u = storage_get_user(i);
    if (strcmp(u->name, name) == 0) {
      // Update PIN if provided
      if (new_pin && strlen(new_pin) > 0) {
        strncpy(u->pin, new_pin, STR_SMALL - 1);
        u->pin[STR_SMALL - 1] = '\0';
        printf("[%s] Updated PIN for user: %s\n", TAG, name);
      }

      // Update emergency PIN if provided
      if (new_emergency_pin && strlen(new_emergency_pin) > 0) {
        strncpy(u->emergency_pin, new_emergency_pin, STR_SMALL - 1);
        u->emergency_pin[STR_SMALL - 1] = '\0';
        printf("[%s] Updated emergency PIN for user: %s\n", TAG, name);
      }

      if (new_phone && strlen(new_phone) > 0)
        strncpy(u->phone, new_phone, STR_SMALL - 1);
      if (new_email && strlen(new_email) > 0)
        strncpy(u->email, new_email, STR_MEDIUM - 1);

      if (new_notify >= 0)
        u->notify = new_notify;
      if (new_is_admin != -1)
        u->is_admin = (new_is_admin == 1);

      storage_save_users();
      return;
    }
  }
}

// --- DELETE ---
void user_drop(const char *name) {
  if (strcmp(name, "Master") == 0) {
    printf("Cannot delete Master user\n");
    return;
  }
  int count = storage_get_user_count();
  for (int i = 0; i < count; i++) {
    user_t *u = storage_get_user(i);
    if (strcmp(u->name, name) == 0) {
      // Shift array
      for (int j = i; j < count - 1; j++) {
        *storage_get_user(j) = *storage_get_user(j + 1);
      }
      storage_set_user_count(count - 1);
      printf("User '%s' deleted\n", name);
      storage_save_users();
      return;
    }
  }
}

bool user_set_totp_secret(const char *name, const char *totp_secret) {
  if (!name)
    return false;

  for (int i = 0; i < storage_get_user_count(); i++) {
    user_t *u = storage_get_user(i);
    if (strcmp(u->name, name) == 0) {
      if (totp_secret && strlen(totp_secret) > 0) {
        strncpy(u->totp_secret, totp_secret, STR_MEDIUM - 1);
        u->totp_secret[STR_MEDIUM - 1] = '\0';
        u->requires_2fa = true;
      } else {
        // Clear TOTP secret
        u->totp_secret[0] = '\0';
        u->requires_2fa = false;
      }
      storage_save_users();
      printf("[%s] Updated TOTP secret for user: %s\n", TAG, name);
      return true;
    }
  }
  return false;
}

// --- DEBUG / UI ---

void user_list(void) {
  printf("--- User List ---\n");
  for (int i = 0; i < storage_get_user_count(); i++) {
    user_t *u = storage_get_user(i);
    printf("[%d] %s (Admin:%s Notify:%d)\n", i, u->name,
           u->is_admin ? "Y" : "N", u->notify);
  }
  printf("-----------------\n");
}

// Helper for the Web UI API
char *users_to_json(void) {
  cJSON *root = cJSON_CreateArray();
  for (int i = 0; i < storage_get_user_count(); i++) {
    cJSON *u = cJSON_CreateObject();
    cJSON_AddStringToObject(u, "name", storage_get_user(i)->name);
    cJSON_AddStringToObject(u, "email", storage_get_user(i)->email);
    cJSON_AddNumberToObject(u, "notify", storage_get_user(i)->notify);
    cJSON_AddBoolToObject(u, "is_admin", storage_get_user(i)->is_admin);
    // We do NOT export PINs over the JSON API for security
    cJSON_AddItemToArray(root, u);
  }
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}