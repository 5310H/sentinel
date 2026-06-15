#include "user_mgr.h" // This now pulls in storage_mgr.h and cJSON.h
#include "../security/password_hash.h"
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
    #include "esp_log.h"
    static const char *TAG = "USER_MGR";
#else
    #define TAG "USER_MGR"
#endif

// Externs are correct - these live in storage_mgr.c memory
extern user_t users[MAX_USERS];
extern int u_count;

void user_add(user_t *users_arr, int *count, const char *name, const char *pin, 
              const char *phone, const char *email, int notify, bool is_admin, const char *emergency_pin) {
    if (*count < MAX_USERS) {
        user_t *u = &users_arr[*count];
        memset(u, 0, sizeof(user_t));
        
        // Using strlcpy (if available) or strncpy safely
        strncpy(u->name, name, STR_SMALL - 1);
        strncpy(u->phone, phone, STR_SMALL - 1);
        strncpy(u->email, email, STR_MEDIUM - 1);
        u->notify = notify;   
        u->is_admin = is_admin;
        
        // Hash the PIN instead of storing plaintext
        if (pin && strlen(pin) > 0) {
            password_hash_t hash;
            if (password_hash_create(pin, &hash)) {
                password_hash_to_hex(&hash, u->pin_hash);
                printf("[%s] Created hashed PIN for user: %s\n", TAG, name);
            } else {
                printf("[%s] WARNING: Failed to hash PIN for %s, user created without PIN\n", TAG, name);
            }
        }
        
        // Store emergency PIN (plaintext for Noonlight dispatcher verification)
        if (emergency_pin && strlen(emergency_pin) > 0) {
            strncpy(u->emergency_pin, emergency_pin, STR_SMALL - 1);
            u->emergency_pin[STR_SMALL - 1] = '\0';
            printf("[%s] Stored emergency PIN for user: %s\n", TAG, name);
        }
        
        (*count)++;
        storage_save_users(); 
    }
}

void user_update(user_t *users_arr, int count, const char *name, 
                 const char *new_pin, const char *new_phone, const char *new_email, 
                 int new_notify, int new_is_admin, const char *new_emergency_pin) {
    for (int i = 0; i < count; i++) {
        if (strcmp(users_arr[i].name, name) == 0) {
            // Hash new PIN if provided
            if (new_pin && strlen(new_pin) > 0) {
                password_hash_t hash;
                if (password_hash_create(new_pin, &hash)) {
                    password_hash_to_hex(&hash, users_arr[i].pin_hash);
                    users_arr[i].pin[0] = '\0';  // Clear legacy plaintext
                    printf("[%s] Updated hashed PIN for user: %s\n", TAG, name);
                } else {
                    printf("[%s] WARNING: Failed to hash new PIN for %s\n", TAG, name);
                }
            }
            
            // Update emergency PIN if provided
            if (new_emergency_pin && strlen(new_emergency_pin) > 0) {
                strncpy(users_arr[i].emergency_pin, new_emergency_pin, STR_SMALL - 1);
                users_arr[i].emergency_pin[STR_SMALL - 1] = '\0';
                printf("[%s] Updated emergency PIN for user: %s\n", TAG, name);
            }
            
            if (new_phone && strlen(new_phone) > 0) strncpy(users_arr[i].phone, new_phone, STR_SMALL - 1);
            if (new_email && strlen(new_email) > 0) strncpy(users_arr[i].email, new_email, STR_MEDIUM - 1);
            
            if (new_notify >= 0) users_arr[i].notify = new_notify;
            if (new_is_admin != -1) users_arr[i].is_admin = (new_is_admin == 1);

            storage_save_users();
            return;
        }
    }
}
// --- DELETE ---
void user_drop(user_t *users, int *u_count, const char *name) {
    for (int i = 0; i < *u_count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            for (int j = i; j < (*u_count) - 1; j++) {
                users[j] = users[j + 1];
            }
            (*u_count)--;
            printf("[%s] Dropped user: %s\n", TAG, name);
            storage_save_users();
            return;
        }
    }
}

bool user_set_totp_secret(user_t *users, int count, const char *name, const char *totp_secret) {
    for (int i = 0; i < count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            if (totp_secret && strlen(totp_secret) > 0) {
                strncpy(users[i].totp_secret, totp_secret, STR_MEDIUM - 1);
                users[i].totp_secret[STR_MEDIUM - 1] = '\0';
                users[i].requires_2fa = true;
            } else {
                // Clear TOTP secret
                users[i].totp_secret[0] = '\0';
                users[i].requires_2fa = false;
            }
            storage_save_users();
            printf("[%s] Updated TOTP secret for user: %s\n", TAG, name);
            return true;
        }
    }
    return false;
}

// --- DEBUG / UI ---

void user_list(user_t *users, int u_count) {
    printf("\n--- SENTINEL USER DATABASE ---\n");
    if (u_count == 0) printf(" (No users found)\n");
    for (int i = 0; i < u_count; i++) {
        printf("[%d] %-12s | Admin: %s | Notify: %d | Email: %s\n", 
               i, users[i].name, users[i].is_admin ? "YES" : "NO ", 
               users[i].notify, users[i].email);
    }
}

// Helper for the Web UI API
char *users_to_json(void) {
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < u_count; i++) {
        cJSON *u = cJSON_CreateObject();
        cJSON_AddStringToObject(u, "name", users[i].name);
        cJSON_AddStringToObject(u, "email", users[i].email);
        cJSON_AddNumberToObject(u, "notify", users[i].notify);
        cJSON_AddBoolToObject(u, "is_admin", users[i].is_admin);
        // We do NOT export PINs over the JSON API for security
        cJSON_AddItemToArray(root, u);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}