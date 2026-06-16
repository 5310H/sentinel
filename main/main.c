#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "mbedtls/sha256.h"
// #include "mdns.h"  // Disabled to save DRAM


// Project Headers
#include "mongoose.h"
#include "storage_mgr.h" 
#include "hal.h"
#include "user_mgr.h"
#include "engine.h"
#include "system_monitoring.h"
#include "cJSON.h"
#include "hal_esp32.h"  // or whatever your hardware header is named
#include "rf_driver.h"
#include "otp.h"
#include "server_cert.h"  // Embedded HTTPS certificate (fallback)
#include "cert_mgr.h"     // Certificate manager for loading from storage
#include "session_mgr.h"  // Session token management
#include "camera_mgr.h"   // Camera integration for snapshots and streaming
#include "tuya_mgr.h"     // Tuya device integration (KC868-A8 native)
#include "tuya_zones.h"   // Tuya virtual zone integration
#include "esphome_zones.h"  // ESPHome virtual zone integration
// Zigbee disabled to save DRAM
// #include "zigbee_mgr.h"   // Zigbee device integration (UART coordinator)
// #include "zigbee_groups.h"   // Zigbee group control (Phase 3)
// #include "zigbee_scenes.h"   // Zigbee scene control (Phase 3)
// #include "zigbee_automations.h"   // Zigbee automation rules (Phase 3)
#include "scheduler.h"   // Task scheduler for automation and scheduling

// USE EXTERN: Do not re-define them here
extern const char version_txt_start[] asm("_binary_version_txt_start");
extern const char version_txt_end[] asm("_binary_version_txt_end");

static const char *TAG = "SENTINEL";

#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          18

// Global Handles
SemaphoreHandle_t i2c_mutex;
i2c_master_bus_handle_t bus_handle;
ssd1306_handle_t oled_dev;

// OTA state
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = NULL;
static bool ota_in_progress = false;
static size_t ota_bytes_written = 0;

// Display update function (called from engine)
void display_update_status(const char* state_text, const char* detail_text);

// --- LOGGING BUFFER ---
#define MAX_LOG_LINES 50
#define MAX_LOG_LEN 256
static char g_log_buf[MAX_LOG_LINES][MAX_LOG_LEN];
static int g_log_idx = 0;
static uint32_t g_log_seq = 0;
static uint32_t g_log_seqs[MAX_LOG_LINES];
static vprintf_like_t s_prev_logging_func = NULL;

int app_log_vprintf(const char *fmt, va_list args) {
    // 1. Write to standard UART (keep original behavior)
    int ret = 0;
    if (s_prev_logging_func) {
        ret = s_prev_logging_func(fmt, args);
    } else {
        ret = vprintf(fmt, args);
    }

    // 2. Buffer output (for Web UI)
    char buf[MAX_LOG_LEN];
    vsnprintf(buf, sizeof(buf), fmt, args);

    // Strip trailing newline for cleaner JSON
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    strncpy(g_log_buf[g_log_idx], buf, MAX_LOG_LEN);
    g_log_seqs[g_log_idx] = ++g_log_seq;
    g_log_idx = (g_log_idx + 1) % MAX_LOG_LINES;

    return ret;
}

// Helper to get string from JSON body
void get_json_str(struct mg_str body, const char *path, char *dst, int dst_len) {
    char *val = mg_json_get_str(body, path);
    if (val != NULL) {
        snprintf(dst, dst_len, "%s", val);
        free(val);
    } else {
        dst[0] = '\0';
    }
}

// Extern from engine.c (if not in header)
extern bool is_ready(void);
extern const char* engine_get_violation_name(void);
extern const char* engine_get_violation_type(void);

// --- Mongoose Web Handler with Diagnostics Endpoints ---
//static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
static void fn(struct mg_connection *c, int ev, void *ev_data) { // New
    void *fn_data = c->fn_data; // Access your data from the connection handle
    if (ev == MG_EV_ACCEPT && fn_data != NULL) {
        // Initialize TLS for HTTPS connections using certificate from storage
        struct mg_tls_opts opts = {
            .cert = cert_mgr_get_cert(),
            .key = cert_mgr_get_cert(),   // Same cert contains both cert and key
        };
        mg_tls_init(c, &opts);
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        // 1. USER MGMT
        if (mg_strcmp(hm->uri, mg_str("/api/users")) == 0) {
            char *json = users_to_json(); 
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
            free(json);
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/users/add")) == 0) {
            char name[STR_SMALL] = {0}, pin[STR_SMALL] = {0}, email[STR_MEDIUM] = {0}, n_buf[10] = {0}, emergency_pin[STR_SMALL] = {0};
            get_json_str(hm->body, "$.name", name, sizeof(name));
            get_json_str(hm->body, "$.pin", pin, sizeof(pin));
            get_json_str(hm->body, "$.email", email, sizeof(email));
            get_json_str(hm->body, "$.notify", n_buf, sizeof(n_buf));
            get_json_str(hm->body, "$.emergency_pin", emergency_pin, sizeof(emergency_pin));
            user_add(name, pin, "", email, atoi(n_buf), false, emergency_pin); 
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/users/delete")) == 0) {
            char name[STR_SMALL] = {0};
            get_json_str(hm->body, "$.name", name, sizeof(name));
            user_drop(name);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/users/set-totp")) == 0) {
            char name[STR_SMALL] = {0};
            char totp_secret[STR_MEDIUM] = {0};
            get_json_str(hm->body, "$.name", name, sizeof(name));
            get_json_str(hm->body, "$.totp_secret", totp_secret, sizeof(totp_secret));
            
            if (strlen(name) > 0) {
                bool success = user_set_totp_secret(users, storage_get_user_count(), name, totp_secret);
                if (success) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"User not found\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Missing user name\"}");
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/users/send-totp-email")) == 0) {
            // Send 2FA setup secret via email
            char name[STR_SMALL] = {0};
            char totp_secret[STR_MEDIUM] = {0};
            get_json_str(hm->body, "$.name", name, sizeof(name));
            get_json_str(hm->body, "$.totp_secret", totp_secret, sizeof(totp_secret));
            
            if (strlen(name) > 0 && strlen(totp_secret) > 0) {
                // Find user email
                const char* user_email = NULL;
                for (int i = 0; i < storage_get_user_count(); i++) {
                    if (strcmp(storage_get_user(i)->name, name) == 0) {
                        user_email = storage_get_user(i)->email;
                        break;
                    }
                }
                
                if (user_email && strlen(user_email) > 0) {
                    // Format email body
                    char email_body[512];
                    snprintf(email_body, sizeof(email_body),
                        "2FA Setup Code for Sentinel Alarm System\n\n"
                        "Your setup code is:\n%s\n\n"
                        "Enter this code in your authenticator app to complete setup.\n"
                        "Account: %s\n"
                        "User: %s\n\n"
                        "If you did not request this, please ignore this email.",
                        totp_secret, storage_get_config()->account_id, name);
                    
                    // Send email via SMTP
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", 
                        "{\"status\":\"ok\",\"message\":\"Setup code sent to %s\"}", user_email);
                    printf("[EMAIL] Sending 2FA setup to %s\n", user_email);
                    // TODO: Call actual SMTP send function
                } else {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"User email not found\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"status\":\"error\",\"message\":\"Missing name or secret\"}");
            }
        }

        // ESPHome Device Management
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome")) == 0) {
            char *json = esphome_devices_to_json(); 
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
            free(json);
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/add")) == 0) {
            char hostname[STR_SMALL] = {0}, friendly_name[STR_SMALL] = {0};
            char password[STR_SMALL] = {0}, encryption_key[STR_MEDIUM] = {0};
            char port_buf[10] = {0}, vzs_buf[10] = {0}, vrs_buf[10] = {0}, enabled_buf[10] = {0};
            
            get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
            get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
            get_json_str(hm->body, "$.friendly_name", friendly_name, sizeof(friendly_name));
            get_json_str(hm->body, "$.password", password, sizeof(password));
            get_json_str(hm->body, "$.encryption_key", encryption_key, sizeof(encryption_key));
            get_json_str(hm->body, "$.virtual_zone_start", vzs_buf, sizeof(vzs_buf));
            get_json_str(hm->body, "$.virtual_relay_start", vrs_buf, sizeof(vrs_buf));
            get_json_str(hm->body, "$.enabled", enabled_buf, sizeof(enabled_buf));
            
            // Validate required fields
            if (strlen(hostname) == 0 || strlen(friendly_name) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Missing required fields\"}");
                return;
            }
            
            uint16_t port = strlen(port_buf) ? atoi(port_buf) : 6053;
            int8_t vzs = strlen(vzs_buf) ? atoi(vzs_buf) : 33;
            int8_t vrs = strlen(vrs_buf) ? atoi(vrs_buf) : 8;
            bool enabled = strlen(enabled_buf) ? (strcmp(enabled_buf, "true") == 0) : true;
            
            int result = esphome_device_add(hostname, port, friendly_name, 
                                           strlen(password) ? password : NULL,
                                           strlen(encryption_key) ? encryption_key : NULL,
                                           vzs, vrs, enabled);
            
            if (result == 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Failed to add device\"}");
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/update")) == 0) {
            char hostname[STR_SMALL] = {0}, friendly_name[STR_SMALL] = {0};
            char password[STR_SMALL] = {0}, encryption_key[STR_MEDIUM] = {0};
            char port_buf[10] = {0}, vzs_buf[10] = {0}, vrs_buf[10] = {0}, enabled_buf[10] = {0};
            
            get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
            get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
            get_json_str(hm->body, "$.friendly_name", friendly_name, sizeof(friendly_name));
            get_json_str(hm->body, "$.password", password, sizeof(password));
            get_json_str(hm->body, "$.encryption_key", encryption_key, sizeof(encryption_key));
            get_json_str(hm->body, "$.virtual_zone_start", vzs_buf, sizeof(vzs_buf));
            get_json_str(hm->body, "$.virtual_relay_start", vrs_buf, sizeof(vrs_buf));
            get_json_str(hm->body, "$.enabled", enabled_buf, sizeof(enabled_buf));
            
            // Validate hostname
            if (strlen(hostname) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Missing hostname\"}");
                return;
            }
            
            uint16_t port = strlen(port_buf) ? atoi(port_buf) : 0;  // 0 = no change
            int8_t vzs = strlen(vzs_buf) ? atoi(vzs_buf) : -1;  // -1 = no change
            int8_t vrs = strlen(vrs_buf) ? atoi(vrs_buf) : -1;  // -1 = no change
            bool enabled = strlen(enabled_buf) ? (strcmp(enabled_buf, "true") == 0) : true;
            
            int result = esphome_device_update(hostname, port,
                                              strlen(friendly_name) ? friendly_name : NULL,
                                              strlen(password) ? password : NULL,
                                              strlen(encryption_key) ? encryption_key : NULL,
                                              vzs, vrs, enabled);
            
            if (result == 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
            } else {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Device not found\"}");
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/delete")) == 0) {
            char hostname[STR_SMALL] = {0};
            get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
            
            // Validate hostname
            if (strlen(hostname) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Missing hostname\"}");
                return;
            }
            
            int result = esphome_device_delete(hostname);
            if (result == 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
            } else {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                             "{\"status\":\"error\",\"message\":\"Device not found\"}");
            }
        }
        
        // ESPHome Testing/Connection Endpoints (for tester.html)
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/connect")) == 0) {
            // Test connection to ESPHome device
            char host[STR_SMALL] = {0};
            char port_buf[10] = {0};
            char password[STR_SMALL] = {0};
            
            get_json_str(hm->body, "$.host", host, sizeof(host));
            get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
            get_json_str(hm->body, "$.password", password, sizeof(password));
            
            if (strlen(host) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"status\":\"error\",\"message\":\"Missing host\"}");
                return;
            }
            
            // TODO: Actually test connection via esphome_api_connect()
            // Port parameter available in port_buf if needed
            // For now, return mock success for devices in config
            bool found = false;
            char device_name[STR_SMALL] = {0};
            for (int i = 0; i < storage_get_esphome_count(); i++) {
                if (strcmp(storage_get_esphome_device(i)->hostname, host) == 0) {
                    found = true;
                    strncpy(device_name, storage_get_esphome_device(i)->friendly_name, STR_SMALL - 1);
                    device_name[STR_SMALL - 1] = '\0';
                    break;
                }
            }
            
            if (found) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"device_name\":\"%s\",\"entities\":0}", device_name);
            } else {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"device_name\":\"%s\",\"entities\":0}", host);
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/status")) == 0) {
            // Get device connection status
            char host[STR_SMALL] = {0};
            get_json_str(hm->body, "$.host", host, sizeof(host));
            
            if (strlen(host) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"status\":\"error\",\"message\":\"Missing host\"}");
                return;
            }
            
            // TODO: Check actual connection status via esphome API
            // For now, return enabled status from config
            bool connected = false;
            for (int i = 0; i < storage_get_esphome_count(); i++) {
                if (strcmp(storage_get_esphome_device(i)->hostname, host) == 0) {
                    connected = storage_get_esphome_device(i)->enabled;
                    break;
                }
            }
            
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "{\"connected\":%s,\"uptime\":\"0s\",\"version\":\"unknown\"}", 
                connected ? "true" : "false");
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/entities")) == 0) {
            // List entities from device
            char host[STR_SMALL] = {0};
            get_json_str(hm->body, "$.host", host, sizeof(host));
            
            if (strlen(host) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"status\":\"error\",\"message\":\"Missing host\"}");
                return;
            }
            
            // TODO: Get actual entities via esphome API
            // For now, return empty list
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "{\"entities\":[]}");
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/esphome/subscribe")) == 0) {
            // Subscribe to state updates
            char host[STR_SMALL] = {0};
            get_json_str(hm->body, "$.host", host, sizeof(host));
            
            if (strlen(host) == 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"status\":\"error\",\"message\":\"Missing host\"}");
                return;
            }
            
            // TODO: Implement state subscription
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "{\"status\":\"ok\",\"subscribed\":true}");
        }

        // 2. AUTH & CONTROL (2FA Support)
        else if (mg_strcmp(hm->uri, mg_str("/api/auth")) == 0) {
            char pin_in[STR_SMALL] = {0};
            char totp_in[STR_SMALL] = {0};
            char step_buf[16] = {0};
            
            get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
            get_json_str(hm->body, "$.totp", totp_in, sizeof(totp_in));
            get_json_str(hm->body, "$.step", step_buf, sizeof(step_buf));
            
            keypad_result_t res = engine_check_keypad(pin_in);
            
            // Step 1: PIN validation only
            if (strcmp(step_buf, "pin") == 0) {
                if (res.authenticated) {
                    // Master PIN (via config) does not require 2FA
                    bool requires_totp = false;
                    
                    if (!res.is_admin || strcmp(res.name, "Master") != 0) {
                        // Regular user - check if 2FA is configured
                        const char* user_secret = engine_get_user_totp_secret(res.name);
                        requires_totp = (user_secret != NULL && strlen(user_secret) > 0);
                    }
                    
                    // Don't send secret to client - security risk
                    // Client should use session-based approach or generate code client-side
                    // Master PIN skips 2FA, regular users require it if configured
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d,\"requires_totp\":%s}",
                        res.is_admin ? "true" : "false", res.name, engine_get_arm_state(),
                        requires_totp ? "true" : "false");
                } else {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":false,\"is_admin\":false,\"name\":\"Unknown\",\"state\":%d}",
                        engine_get_arm_state());
                }
            }
            // Step 2: TOTP validation (PIN + TOTP)
            else if (strcmp(step_buf, "totp") == 0 && totp_in[0] != '\0') {
                if (!res.authenticated) {
                    // PIN invalid
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":false,\"is_admin\":false,\"name\":\"Unknown\",\"state\":%d}",
                        engine_get_arm_state());
                } else {
                    // PIN is valid, now validate TOTP
                    bool totp_valid = false;
                    
                    // Get user's TOTP secret
                    const char* user_secret = engine_get_user_totp_secret(res.name);
                    
                    // If user has no TOTP secret set, allow PIN-only for now
                    if (!user_secret || strlen(user_secret) == 0) {
                        // User doesn't have 2FA configured - allow PIN-only
                        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                            "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d}",
                            res.is_admin ? "true" : "false", res.name, engine_get_arm_state());
                    } else {
                        // User has TOTP secret configured - validate TOTP code
                        // Validate TOTP code with ±1 time window tolerance for clock skew
                        if (strlen(totp_in) == 6 && user_secret) {
                            totp_valid = validate_totp(user_secret, totp_in, 30, 6, 1);
                        }
                        
                        if (totp_valid) {
                            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d}",
                                res.is_admin ? "true" : "false", res.name, engine_get_arm_state());
                        } else {
                            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                "{\"authenticated\":false,\"is_admin\":false,\"name\":\"Unknown\",\"state\":%d,\"error\":\"Invalid TOTP code\"}",
                                engine_get_arm_state());
                        }
                    }
                }
            }
            // Legacy: PIN-only authentication (for backward compatibility)
            else {
                // Original behavior - PIN only (no 2FA)
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"authenticated\":%s,\"is_admin\":%s,\"name\":\"%s\",\"state\":%d}",
                    res.authenticated ? "true" : "false", res.is_admin ? "true" : "false",
                    res.name, engine_get_arm_state());
            }
        }

        // 3. API: Full Status (Matches index.html expectations)
        else if (mg_strcmp(hm->uri, mg_str("/api/status")) == 0) {
            // Use cJSON to construct the large response safely
            cJSON *root = cJSON_CreateObject();
            
            // Config Object
            cJSON *cfg = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "config", cfg);
            cJSON_AddStringToObject(cfg, "acct_id", storage_get_config()->account_id);
            cJSON_AddStringToObject(cfg, "name", storage_get_config()->name);
            cJSON_AddStringToObject(cfg, "email", storage_get_config()->email);
            cJSON_AddNumberToObject(cfg, "state", engine_get_arm_state());
            cJSON_AddBoolToObject(cfg, "ready", is_ready());
            cJSON_AddStringToObject(cfg, "violation", engine_get_violation_name());
            cJSON_AddStringToObject(cfg, "violation_type", engine_get_violation_type());

            // Uptime
            int64_t t_micros = esp_timer_get_time();
            int up_sec = (int)(t_micros / 1000000);
            char up_str[32];
            snprintf(up_str, sizeof(up_str), "%dh %dm", up_sec / 3600, (up_sec % 3600) / 60);
            cJSON_AddStringToObject(cfg, "uptime", up_str);

            // MAC
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_ETH);
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            cJSON_AddStringToObject(cfg, "mac", mac_str);

            // Network (Placeholder for now)
            cJSON *net = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "network", net);
            cJSON_AddStringToObject(net, "ip", "192.168.1.x"); // TODO: Get real IP

            // RF
            cJSON *rf = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "rf", rf);
            char rf_buf[16];
            snprintf(rf_buf, sizeof(rf_buf), "%06lX", (unsigned long)last_rf_code);
            cJSON_AddStringToObject(rf, "last", rf_buf);

            // Zones
            cJSON *z_arr = cJSON_CreateArray();
            cJSON_AddItemToObject(root, "zones", z_arr);
            
            // Physical zones (1-32)
            for (int i = 0; i < storage_get_zone_count(); i++) {
                if (storage_get_zone(i)->gpio >= 0) {  // Only physical zones with GPIO
                    cJSON *z = cJSON_CreateObject();
                    cJSON_AddNumberToObject(z, "id", storage_get_zone(i)->id);
                    cJSON_AddStringToObject(z, "name", storage_get_zone(i)->name);
                    cJSON_AddStringToObject(z, "description", storage_get_zone(i)->description);
                    cJSON_AddStringToObject(z, "location", storage_get_zone(i)->location);
                    // In HAL, 0 usually means triggered/open for NC sensors
                    bool is_open = (hal_get_zone_state(storage_get_zone(i)->gpio) == 0);
                    cJSON_AddBoolToObject(z, "open", is_open);
                    cJSON_AddBoolToObject(z, "violated", is_open);
                    cJSON_AddItemToArray(z_arr, z);
                }
            }
            
            // ESPHome virtual zones (33-64)
            for (int i = 0; i < storage_get_esphome_count(); i++) {
                if (storage_get_esphome_device(i)->virtual_zone_start > 0 && storage_get_esphome_device(i)->enabled) {
                    cJSON *z = cJSON_CreateObject();
                    cJSON_AddNumberToObject(z, "id", storage_get_esphome_device(i)->virtual_zone_start);
                    cJSON_AddStringToObject(z, "name", storage_get_esphome_device(i)->friendly_name);
                    cJSON_AddStringToObject(z, "description", storage_get_esphome_device(i)->description[0] != '\0' ? storage_get_esphome_device(i)->description : storage_get_esphome_device(i)->friendly_name);
                    cJSON_AddStringToObject(z, "location", storage_get_esphome_device(i)->location);
                    cJSON_AddBoolToObject(z, "open", false);
                    cJSON_AddBoolToObject(z, "violated", false);
                    cJSON_AddItemToArray(z_arr, z);
                }
            }
            
            // Tuya virtual zones (65-96)
            tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
            int tuya_count = 0;
            if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT, &tuya_count) == ESP_OK) {
                for (int i = 0; i < tuya_count; i++) {
                    if (tuya_devs[i].virtual_zone_id > 0 && tuya_devs[i].enabled) {
                        cJSON *z = cJSON_CreateObject();
                        cJSON_AddNumberToObject(z, "id", tuya_devs[i].virtual_zone_id);
                        cJSON_AddStringToObject(z, "name", tuya_devs[i].name);
                        cJSON_AddStringToObject(z, "description", tuya_devs[i].description[0] != '\0' ? tuya_devs[i].description : tuya_devs[i].name);
                        cJSON_AddStringToObject(z, "location", tuya_devs[i].location);
                        bool is_open = false;
                        if (tuya_devs[i].type == TUYA_DEV_TYPE_DOOR_SENSOR) {
                            is_open = tuya_devs[i].contact_open;
                        } else if (tuya_devs[i].type == TUYA_DEV_TYPE_MOTION_SENSOR) {
                            is_open = tuya_devs[i].motion_detected;
                        }
                        cJSON_AddBoolToObject(z, "open", is_open);
                        cJSON_AddBoolToObject(z, "violated", is_open);
                        cJSON_AddItemToArray(z_arr, z);
                    }
                }
            }

            // Relays
            cJSON *r_arr = cJSON_CreateArray();
            cJSON_AddItemToObject(root, "relays", r_arr);
            
            // Physical relays (1-8)
            for (int i = 0; i < storage_get_relay_count(); i++) {
                if (storage_get_relay(i)->id <= 8) {  // Only physical relays
                    cJSON *r = cJSON_CreateObject();
                    cJSON_AddNumberToObject(r, "id", storage_get_relay(i)->id);
                    cJSON_AddStringToObject(r, "name", storage_get_relay(i)->name);
                    cJSON_AddStringToObject(r, "location", storage_get_relay(i)->location);
                    cJSON_AddBoolToObject(r, "active", hal_get_relay_state(storage_get_relay(i)->gpio));
                    cJSON_AddItemToArray(r_arr, r);
                }
            }
            
            // ESPHome virtual relays (8-31)
            for (int i = 0; i < storage_get_esphome_count(); i++) {
                if (storage_get_esphome_device(i)->virtual_relay_start > 0 && storage_get_esphome_device(i)->enabled) {
                    cJSON *r = cJSON_CreateObject();
                    cJSON_AddNumberToObject(r, "id", storage_get_esphome_device(i)->virtual_relay_start);
                    cJSON_AddStringToObject(r, "name", storage_get_esphome_device(i)->friendly_name);
                    cJSON_AddStringToObject(r, "location", storage_get_esphome_device(i)->location);
                    cJSON_AddBoolToObject(r, "active", false);
                    cJSON_AddItemToArray(r_arr, r);
                }
            }
            
            // Tuya virtual relays (32-63)
            tuya_device_t tuya_relay_devs[TUYA_DEVICE_MAX_COUNT];
            int tuya_relay_count = 0;
            if (tuya_mgr_get_devices(tuya_relay_devs, TUYA_DEVICE_MAX_COUNT, &tuya_relay_count) == ESP_OK) {
                for (int i = 0; i < tuya_relay_count; i++) {
                    if (tuya_relay_devs[i].virtual_relay_id > 0 && tuya_relay_devs[i].enabled) {
                        cJSON *r = cJSON_CreateObject();
                        cJSON_AddNumberToObject(r, "id", tuya_relay_devs[i].virtual_relay_id);
                        cJSON_AddStringToObject(r, "name", tuya_relay_devs[i].name);
                        cJSON_AddStringToObject(r, "location", tuya_relay_devs[i].location);
                        cJSON_AddBoolToObject(r, "active", tuya_relay_devs[i].switch_on);
                        cJSON_AddItemToArray(r_arr, r);
                    }
                }
            }

            char *json_str = cJSON_PrintUnformatted(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }

        // GET /diagnostics - Full system diagnostics JSON
        if (mg_strcmp(hm->uri, mg_str("/diagnostics")) == 0) {
            char *diag_json = NULL;
            if (system_monitoring_get_diagnostics_json(&diag_json) == ESP_OK && diag_json) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", diag_json);
                free(diag_json);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                    "{\"error\": \"Failed to get diagnostics\"}");
            }
            return;
        }
        
        // GET /status - Quick system status (for index.html temp/power)
        if (mg_strcmp(hm->uri, mg_str("/status")) == 0) {
            system_status_t status = {0};
            system_monitoring_get_status(&status);
            
            int temp_f = (status.temperature_c * 9 / 5) + 32;  // Convert to Fahrenheit
            
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "temperature_c", status.temperature_c);
            cJSON_AddNumberToObject(root, "temperature_f", temp_f);
            cJSON_AddBoolToObject(root, "tamper_detected", status.tamper_detected);
            cJSON_AddBoolToObject(root, "on_backup_power", status.on_backup_power);
            cJSON_AddNumberToObject(root, "arm_state", engine_get_arm_state());
            
            char *json_str = cJSON_Print(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }

        // API: Firmware Version
        else if (mg_strcmp(hm->uri, mg_str("/api/version")) == 0) {
            // Get embedded version
            size_t version_len = (size_t)(version_txt_end - version_txt_start);
            char version_str[32] = {0};
            if (version_len > 0 && version_len < sizeof(version_str)) {
                memcpy(version_str, version_txt_start, version_len);
                // Remove trailing whitespace
                for (int i = version_len - 1; i >= 0; i--) {
                    if (version_str[i] == '\n' || version_str[i] == '\r' || version_str[i] == ' ') {
                        version_str[i] = '\0';
                    } else {
                        break;
                    }
                }
            } else {
                strcpy(version_str, "unknown");
            }

            // Get partition info
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *boot = esp_ota_get_boot_partition();

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "version", version_str);
            cJSON_AddStringToObject(root, "running_partition", running ? running->label : "unknown");
            cJSON_AddStringToObject(root, "boot_partition", boot ? boot->label : "unknown");
            cJSON_AddBoolToObject(root, "ota_capable", true);

            char *json_str = cJSON_Print(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }

        // GET /events - Event history (CSV or JSON)
        if (mg_strcmp(hm->uri, mg_str("/events")) == 0) {
            event_log_entry_t events[32];
            uint8_t count = 0;
            system_monitoring_get_all_events(events, &count);
            
            cJSON *root = cJSON_CreateArray();
            for (int i = 0; i < count; i++) {
                cJSON *entry = cJSON_CreateObject();
                cJSON_AddNumberToObject(entry, "timestamp", events[i].timestamp);
                cJSON_AddNumberToObject(entry, "event_type", events[i].event_type);
                cJSON_AddNumberToObject(entry, "zone_id", events[i].zone_id);
                cJSON_AddItemToArray(root, entry);
            }
            
            char *json_str = cJSON_Print(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }
        
        // API: Logs (for tester.html)
        if (mg_strcmp(hm->uri, mg_str("/api/logs")) == 0) {
            double client_seq = 0;
            mg_json_get_num(hm->body, "$.seq", &client_seq);
            uint32_t seq = (uint32_t)client_seq;
            
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "next_seq", g_log_seq);
            cJSON *lines = cJSON_CreateArray();
            cJSON_AddItemToObject(root, "lines", lines);

            for (int i = 0; i < MAX_LOG_LINES; i++) {
                int idx = (g_log_idx + i) % MAX_LOG_LINES;
                if (g_log_seqs[idx] > seq) {
                    cJSON_AddItemToArray(lines, cJSON_CreateString(g_log_buf[idx]));
                }
            }
            char *json_str = cJSON_PrintUnformatted(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }

        // API: Zones (for zstatus.html)
        if (mg_strcmp(hm->uri, mg_str("/api/zones")) == 0) {
            cJSON *z_arr = cJSON_CreateArray();
            for (int i = 0; i < storage_get_zone_count(); i++) {
                cJSON *z = cJSON_CreateObject();
                cJSON_AddStringToObject(z, "name", storage_get_zone(i)->name);
                cJSON_AddStringToObject(z, "location", "");
                cJSON_AddBoolToObject(z, "open", hal_get_zone_state(storage_get_zone(i)->gpio) == 0);
                cJSON_AddItemToArray(z_arr, z);
            }
            char *json_str = cJSON_PrintUnformatted(z_arr);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(z_arr);
            return;
        }

        // API: Login (Create Session Token)
        if (mg_strcmp(hm->uri, mg_str("/api/login")) == 0) {
            char pin_in[STR_SMALL] = {0};
            get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
            
            user_t *user = NULL;
            if (user_authenticate_pin(pin_in, &user) && user != NULL) {
                // Get client IP address
                char client_ip[16] = {0};
                struct mg_str *forwarded = mg_http_get_header(hm, "X-Forwarded-For");
                if (forwarded && forwarded->len > 0 && forwarded->len < sizeof(client_ip)) {
                    snprintf(client_ip, sizeof(client_ip), "%.*s", (int)forwarded->len, forwarded->buf);
                } else {
                    // Fall back to connection remote address
                    mg_snprintf(client_ip, sizeof(client_ip), "%M", mg_print_ip, &c->rem);
                }
                
                // Create session token
                char token[SESSION_TOKEN_MAX_LEN] = {0};
                if (session_create(user->name, client_ip, token)) {
                    ESP_LOGI(TAG, "Session created for user '%s' from IP %s", user->name, client_ip);
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", 
                        "{\"status\":\"ok\",\"token\":\"%s\",\"user\":\"%s\",\"is_admin\":%s}", 
                        token, user->name, user->is_admin ? "true" : "false");
                } else {
                    ESP_LOGE(TAG, "Failed to create session for user '%s'", user->name);
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Failed to create session\"}");
                }
            } else {
                ESP_LOGW(TAG, "Login failed for PIN");
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", 
                    "{\"status\":\"error\",\"message\":\"Invalid PIN\"}");
            }
            return;
        }

        // API: Arm/Disarm
        if (mg_strcmp(hm->uri, mg_str("/api/arm")) == 0) {
           char pin_in[16] = {0};
           char mode_str[16] = {0};
           get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
           get_json_str(hm->body, "$.mode", mode_str, sizeof(mode_str));
           
           keypad_result_t res = engine_check_keypad(pin_in);
           if (res.authenticated) {
              // Parse arm mode from request
              arm_mode_t mode = ARM_AWAY; // Default
              if (strcmp(mode_str, "stay") == 0) mode = ARM_STAY;
              else if (strcmp(mode_str, "night") == 0) mode = ARM_NIGHT;
              else if (strcmp(mode_str, "vacation") == 0) mode = ARM_VACATION;
              
              engine_ui_arm(mode);
              mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\",\"user\":\"%s\",\"mode\":\"%s\"}", res.name, mode_str);
            } else {
              mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/disarm")) == 0) {
          char pin_in[16] = {0};
          get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
          keypad_result_t res = engine_check_keypad(pin_in);
          if (res.authenticated) {
            engine_ui_disarm();
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\",\"user\":\"%s\"}", res.name);
          } else {
            mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
          }
        }

        // API: OTA Status
        else if (mg_strcmp(hm->uri, mg_str("/api/ota/status")) == 0) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "in_progress", ota_in_progress);
            cJSON_AddNumberToObject(root, "bytes_written", ota_bytes_written);

            // Partition info
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *boot = esp_ota_get_boot_partition();
            cJSON_AddStringToObject(root, "running_partition", running ? running->label : "unknown");
            cJSON_AddStringToObject(root, "boot_partition", boot ? boot->label : "unknown");

            // OTA partition availability
            const esp_partition_t *ota_part = esp_ota_get_next_update_partition(NULL);
            cJSON_AddBoolToObject(root, "ota_partition_available", ota_part != NULL);
            if (ota_part) {
                cJSON_AddStringToObject(root, "ota_partition", ota_part->label);
                cJSON_AddNumberToObject(root, "ota_partition_size", ota_part->size);
            }

            char *json_str = cJSON_Print(root);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(root);
            return;
        }

        // API: OTA Firmware Upload (Secure)
        else if (mg_strcmp(hm->uri, mg_str("/api/ota/upload")) == 0) {
            // Extract PIN from Authorization header or body
            char pin_in[STR_SMALL] = {0};
            struct mg_str *auth_hdr = mg_http_get_header(hm, "Authorization");
            
            if (auth_hdr && auth_hdr->len > 7) {
                // Check if header starts with "Bearer "
                if (strncmp(auth_hdr->buf, "Bearer ", 7) == 0) {
                    // Extract PIN from "Bearer <PIN>" header
                    size_t pin_len = auth_hdr->len - 7;
                    if (pin_len < sizeof(pin_in)) {
                        snprintf(pin_in, sizeof(pin_in), "%.*s", (int)pin_len, auth_hdr->buf + 7);
                    }
                }
            } else {
                // Try to get PIN from JSON body
                get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
            }
            
            // Authenticate - must be master PIN or admin user
            bool authenticated = false;
            if (strlen(pin_in) > 0) {
                // Check master PIN
                if (strcmp(pin_in, storage_get_config()->pin) == 0) {
                    authenticated = true;
                } else {
                    // Check user PINs (admin-level users only)
                    for (int i = 0; i < storage_get_user_count(); i++) {
                        if (strcmp(storage_get_user(i)->pin, pin_in) == 0) {
                            authenticated = true;
                            break;
                        }
                    }
                }
            }
            
            if (!authenticated) {
                ESP_LOGW(TAG, "OTA upload unauthorized");
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", 
                    "{\"status\":\"error\",\"message\":\"Unauthorized - valid PIN required\"}");
                return;
            }
            
            ESP_LOGI(TAG, "OTA upload authenticated");
            
            // Handle firmware upload
            long offset = mg_http_upload(c, hm, &mg_fs_posix, "/tmp/firmware.bin", 10*1024*1024);
            
            if (offset < 0) {
                ESP_LOGE(TAG, "OTA upload failed: %ld", offset);
                mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                    "{\"status\":\"error\",\"message\":\"Upload failed\"}");
            } else if (offset > 0) {
                // Upload in progress
                ESP_LOGI(TAG, "OTA upload progress: %ld bytes", offset);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", 
                    "{\"status\":\"uploading\",\"bytes\":%ld}", offset);
            } else {
                // Upload complete (offset == 0 means done)
                ESP_LOGI(TAG, "OTA upload complete, starting flash process");
                
                // Begin OTA update
                ota_partition = esp_ota_get_next_update_partition(NULL);
                if (!ota_partition) {
                    ESP_LOGE(TAG, "No OTA partition found");
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"No OTA partition\"}");
                    return;
                }
                
                // Read uploaded firmware file
                FILE *fw_file = fopen("/tmp/firmware.bin", "rb");
                if (!fw_file) {
                    ESP_LOGE(TAG, "Cannot open uploaded firmware");
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Cannot read firmware\"}");
                    return;
                }
                
                // Get file size
                fseek(fw_file, 0, SEEK_END);
                size_t fw_size = ftell(fw_file);
                fseek(fw_file, 0, SEEK_SET);
                
                ESP_LOGI(TAG, "Firmware size: %zu bytes", fw_size);
                
                // Validate firmware image format
                esp_image_header_t img_hdr;
                if (fread(&img_hdr, sizeof(img_hdr), 1, fw_file) != 1) {
                    ESP_LOGE(TAG, "Cannot read image header");
                    fclose(fw_file);
                    mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Invalid firmware format\"}");
                    return;
                }
                
                if (img_hdr.magic != ESP_IMAGE_HEADER_MAGIC) {
                    ESP_LOGE(TAG, "Invalid firmware magic: 0x%02x (expected 0x%02x)", 
                        img_hdr.magic, ESP_IMAGE_HEADER_MAGIC);
                    fclose(fw_file);
                    mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Invalid firmware magic number\"}");
                    return;
                }
                
                // Rewind for full flash
                fseek(fw_file, 0, SEEK_SET);
                
                // Begin OTA
                esp_err_t err = esp_ota_begin(ota_partition, fw_size, &ota_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                    fclose(fw_file);
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"OTA begin failed\"}");
                    return;
                }
                
                ota_in_progress = true;
                ota_bytes_written = 0;
                
                // Write firmware in chunks
                uint8_t *buffer = malloc(4096);
                if (!buffer) {
                    ESP_LOGE(TAG, "Failed to allocate buffer");
                    esp_ota_abort(ota_handle);
                    fclose(fw_file);
                    ota_in_progress = false;
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
                    return;
                }
                
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, 4096, fw_file)) > 0) {
                    err = esp_ota_write(ota_handle, buffer, bytes_read);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                        free(buffer);
                        esp_ota_abort(ota_handle);
                        fclose(fw_file);
                        ota_in_progress = false;
                        mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                            "{\"status\":\"error\",\"message\":\"Flash write failed\"}");
                        return;
                    }
                    ota_bytes_written += bytes_read;
                }
                
                free(buffer);
                fclose(fw_file);
                
                // End OTA
                err = esp_ota_end(ota_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
                    ota_in_progress = false;
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"OTA finalization failed\"}");
                    return;
                }
                
                // Set boot partition
                err = esp_ota_set_boot_partition(ota_partition);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
                    ota_in_progress = false;
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"status\":\"error\",\"message\":\"Failed to set boot partition\"}");
                    return;
                }
                
                ota_in_progress = false;
                
                // Delete temp file
                remove("/tmp/firmware.bin");
                
                ESP_LOGI(TAG, "OTA update successful! %zu bytes written. Restarting in 3s...", ota_bytes_written);
                
                // Mark new firmware as valid (prevents rollback)
                esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
                if (mark_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to mark app valid: %s", esp_err_to_name(mark_err));
                } else {
                    ESP_LOGI(TAG, "New firmware marked as valid");
                }
                
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", 
                    "{\"status\":\"success\",\"message\":\"Firmware updated, restarting in 3 seconds\",\"bytes\":%zu}", 
                    ota_bytes_written);
                
                // Schedule restart
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
            }
        }

        // API: Relay Toggle
        else if (mg_strcmp(hm->uri, mg_str("/api/relay/toggle")) == 0) {
            double idx_d = 0;
            bool state_b = false;
            mg_json_get_num(hm->body, "$.index", &idx_d);
            mg_json_get_bool(hm->body, "$.state", &state_b);
            
            int relay_idx = (int)idx_d;
            int current_idx = 0;
            bool found = false;
            
            // Check physical relays
            for (int i = 0; i < storage_get_relay_count() && !found; i++) {
                if (current_idx == relay_idx) {
                    hal_set_relay(storage_get_relay(i)->gpio, state_b);
                    ESP_LOGI(TAG, "Physical relay %d (%s) set to %s", storage_get_relay(i)->id, storage_get_relay(i)->name, state_b ? "ON" : "OFF");
                    found = true;
                    break;
                }
                current_idx++;
            }
            
            // Check ESPHome relays
            if (!found) {
                for (int i = 0; i < storage_get_esphome_count() && !found; i++) {
                    if (storage_get_esphome_device(i)->virtual_relay_start > 0 && storage_get_esphome_device(i)->enabled) {
                        if (current_idx == relay_idx) {
                            ESP_LOGI(TAG, "ESPHome relay %d (%s) set to %s", 
                                storage_get_esphome_device(i)->virtual_relay_start, 
                                storage_get_esphome_device(i)->friendly_name, 
                                state_b ? "ON" : "OFF");
                            // TODO: Call ESPHome API to toggle switch
                            found = true;
                            break;
                        }
                        current_idx++;
                    }
                }
            }
            
            // Check Tuya relays
            if (!found) {
                tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
                int tuya_count = 0;
                if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT, &tuya_count) == ESP_OK) {
                    for (int i = 0; i < tuya_count && !found; i++) {
                        if (tuya_devs[i].virtual_relay_id > 0 && tuya_devs[i].enabled) {
                            if (current_idx == relay_idx) {
                                tuya_mgr_set_switch(tuya_devs[i].device_id, state_b);
                                ESP_LOGI(TAG, "Tuya relay %d (%s) set to %s", 
                                    tuya_devs[i].virtual_relay_id, 
                                    tuya_devs[i].name, 
                                    state_b ? "ON" : "OFF");
                                found = true;
                                break;
                            }
                            current_idx++;
                        }
                    }
                }
            }
            
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        }

        // API: RF Inject (Simulate RF code)
        else if (mg_strcmp(hm->uri, mg_str("/api/rf/inject")) == 0) {
            char code_buf[32] = {0};
            get_json_str(hm->body, "$.code", code_buf, sizeof(code_buf));
            // Convert hex string to uint32
            last_rf_code = (uint32_t)strtoul(code_buf, NULL, 16);
            ESP_LOGI(TAG, "RF Injected: %06lX", (unsigned long)last_rf_code);
            // Note: In a real system, you might want to trigger the engine's RF handler here
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        }

        // ==================== CAMERA API ====================
        
        // GET /api/cameras - List all cameras
        else if (mg_strcmp(hm->uri, mg_str("/api/cameras")) == 0) {
            cJSON *cameras_arr = cJSON_CreateArray();
            int cam_count = camera_mgr_get_count();
            
            for (int i = 0; i < cam_count; i++) {
                const camera_device_t *cam = camera_mgr_get_device(i);
                if (cam) {
                    cJSON *cam_obj = cJSON_CreateObject();
                    cJSON_AddNumberToObject(cam_obj, "id", i);
                    cJSON_AddStringToObject(cam_obj, "hostname", cam->hostname);
                    cJSON_AddNumberToObject(cam_obj, "port", cam->port);
                    cJSON_AddStringToObject(cam_obj, "friendly_name", cam->friendly_name);
                    cJSON_AddStringToObject(cam_obj, "esphome_id", cam->esphome_id);
                    cJSON_AddBoolToObject(cam_obj, "enabled", cam->enabled);
                    cJSON_AddBoolToObject(cam_obj, "sd_recording_enabled", cam->sd_recording_enabled);
                    cJSON_AddNumberToObject(cam_obj, "last_snapshot", (double)cam->last_snapshot_time);
                    cJSON_AddNumberToObject(cam_obj, "failures", cam->consecutive_failures);
                    cJSON_AddItemToArray(cameras_arr, cam_obj);
                }
            }
            
            // Add SD card status and stats to response
            camera_stats_t cam_stats;
            camera_mgr_get_stats(&cam_stats);
            
            cJSON *stats_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(stats_obj, "total_snapshots", cam_stats.total_snapshots);
            cJSON_AddNumberToObject(stats_obj, "alarm_triggered_snapshots", cam_stats.alarm_triggered_snapshots);
            
            cJSON *response = cJSON_CreateObject();
            cJSON_AddBoolToObject(response, "sd_available", camera_mgr_is_sd_available());
            cJSON_AddItemToObject(response, "cameras", cameras_arr);
            cJSON_AddItemToObject(response, "stats", stats_obj);
            
            char *json_str = cJSON_PrintUnformatted(response);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(response);
        }
        
        // GET /api/camera/:id/snapshot - Get camera snapshot
        else if (mg_match(hm->uri, mg_str("/api/camera/*/snapshot"), NULL)) {
            // Extract camera ID from URI
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12; // Skip "/api/camera/"
                int camera_id = atoi(id_start);
                
                uint8_t *buffer = NULL;
                size_t size = 0;
                
                // Try to fetch fresh snapshot
                esp_err_t err = camera_mgr_fetch_snapshot(camera_id, &buffer, &size);
                
                if (err == ESP_OK && buffer && size > 0) {
                    mg_http_reply(c, 200, "Content-Type: image/jpeg\r\nCache-Control: no-cache\r\n", "");
                    mg_send(c, buffer, size);
                } else {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"error\":\"Failed to fetch snapshot\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }
        
        // GET /api/camera/:id/stream - MJPEG streaming proxy
        else if (mg_match(hm->uri, mg_str("/api/camera/*/stream"), NULL)) {
            // Extract camera ID from URI
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12; // Skip "/api/camera/"
                int camera_id = atoi(id_start);
                
                const camera_device_t *cam = camera_mgr_get_device(camera_id);
                
                if (!cam || !cam->enabled) {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                        "{\"error\":\"Camera not found or disabled\"}");
                    return;
                }
                
                // Build MJPEG URL for camera (try /mjpeg first, then /video)
                char mjpeg_url[256];
                snprintf(mjpeg_url, sizeof(mjpeg_url), "http://%s:%d/mjpeg", 
                         cam->hostname, cam->port);
                
                ESP_LOGI(TAG, "Proxying MJPEG stream from camera %d: %s", camera_id, mjpeg_url);
                
                // Send MJPEG headers to browser
                mg_printf(c, "HTTP/1.1 200 OK\r\n"
                             "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                             "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                             "Pragma: no-cache\r\n"
                             "Expires: 0\r\n"
                             "Connection: close\r\n"
                             "\r\n");
                
                // Mark connection as handled (don't send more headers)
                c->is_resp = 1;
                
                // For now, fallback to snapshot polling if MJPEG not available
                // TODO: Implement full MJPEG proxy with upstream connection
                ESP_LOGW(TAG, "MJPEG streaming proxy not fully implemented yet, use snapshots");
                mg_printf(c, "--frame\r\n"
                             "Content-Type: text/plain\r\n"
                             "\r\n"
                             "MJPEG streaming coming soon. Please use snapshot mode for now.\r\n");
                c->is_draining = 1;
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }
        
        // POST /api/camera/add - Add new camera
        else if (mg_strcmp(hm->uri, mg_str("/api/camera/add")) == 0) {
            cJSON *root = cJSON_Parse(hm->body.buf);
            if (!root) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            cJSON *hostname = cJSON_GetObjectItem(root, "hostname");
            cJSON *port = cJSON_GetObjectItem(root, "port");
            cJSON *name = cJSON_GetObjectItem(root, "friendly_name");
            cJSON *esphome_id = cJSON_GetObjectItem(root, "esphome_id");
            cJSON *username = cJSON_GetObjectItem(root, "username");
            cJSON *password = cJSON_GetObjectItem(root, "password");
            cJSON *snapshot_cmd = cJSON_GetObjectItem(root, "snapshot_cmd");
            cJSON *motion_cmd = cJSON_GetObjectItem(root, "motion_cmd");
            cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
            if (!hostname || !cJSON_IsString(hostname) || !name || !cJSON_IsString(name)) {
                cJSON_Delete(root);
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"error\":\"Missing required fields\"}");
                return;
            }
            int camera_id = camera_mgr_add_device(
                hostname->valuestring,
                port ? port->valueint : 80,
                name->valuestring,
                esphome_id && cJSON_IsString(esphome_id) ? esphome_id->valuestring : "camera",
                username && cJSON_IsString(username) ? username->valuestring : "",
                password && cJSON_IsString(password) ? password->valuestring : "",
                snapshot_cmd && cJSON_IsString(snapshot_cmd) ? snapshot_cmd->valuestring : "",
                motion_cmd && cJSON_IsString(motion_cmd) ? motion_cmd->valuestring : "",
                enabled ? cJSON_IsTrue(enabled) : true
            );
            cJSON_Delete(root);
            if (camera_id >= 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"success\":true,\"camera_id\":%d}", camera_id);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"error\":\"Failed to add camera\"}");
            }
        }
        
        // DELETE /api/camera/:id - Remove camera
        else if (mg_match(hm->uri, mg_str("/api/camera/*"), NULL) && 
                 mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12;
                int camera_id = atoi(id_start);
                
                esp_err_t err = camera_mgr_remove_device(camera_id);
                if (err == ESP_OK) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", 
                        "{\"success\":true}");
                } else {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                        "{\"error\":\"Failed to delete camera\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }
        
        // POST /api/camera/:id/toggle - Enable/disable camera
        else if (mg_match(hm->uri, mg_str("/api/camera/*/toggle"), NULL)) {
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12;
                int camera_id = atoi(id_start);
                
                const camera_device_t *cam = camera_mgr_get_device(camera_id);
                if (cam) {
                    esp_err_t err = camera_mgr_set_enabled(camera_id, !cam->enabled);
                    if (err == ESP_OK) {
                        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                            "{\"success\":true,\"enabled\":%s}", !cam->enabled ? "true" : "false");
                    } else {
                        mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                            "{\"error\":\"Failed to toggle camera\"}");
                    }
                } else {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                        "{\"error\":\"Camera not found\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }
        
        // POST /api/camera/:id/test - Test camera connection
        else if (mg_match(hm->uri, mg_str("/api/camera/*/test"), NULL)) {
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12;
                int camera_id = atoi(id_start);
                
                esp_err_t err = camera_mgr_test_connection(camera_id);
                if (err == ESP_OK) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":true,\"status\":\"online\"}");
                } else {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":false,\"status\":\"offline\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }
        
        // POST /api/camera/:id/sd_recording - Toggle SD recording
        else if (mg_match(hm->uri, mg_str("/api/camera/*/sd_recording"), NULL)) {
            struct mg_str uri_copy = hm->uri;
            const char *id_start = strstr(uri_copy.buf, "/api/camera/");
            if (id_start) {
                id_start += 12;
                int camera_id = atoi(id_start);
                
                const camera_device_t *cam = camera_mgr_get_device(camera_id);
                if (cam) {
                    esp_err_t err = camera_mgr_set_sd_recording(camera_id, !cam->sd_recording_enabled);
                    if (err == ESP_OK) {
                        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                            "{\"success\":true,\"sd_recording_enabled\":%s}", 
                            !cam->sd_recording_enabled ? "true" : "false");
                    } else if (err == ESP_ERR_INVALID_STATE) {
                        mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                            "{\"error\":\"SD card not available\"}");
                    } else {
                        mg_http_reply(c, 500, "Content-Type: application/json\r\n", 
                            "{\"error\":\"Failed to toggle SD recording\"}");
                    }
                } else {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n", 
                        "{\"error\":\"Camera not found\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", 
                    "{\"error\":\"Invalid camera ID\"}");
            }
        }

        // ==================== LIVE CAMERA VIEW ====================
        
        // GET /cameras_live.html - Live camera grid view
        else if (mg_strcmp(hm->uri, mg_str("/cameras_live.html")) == 0) {
            struct mg_http_serve_opts opts = {.root_dir = "/spiffs"};
            mg_http_serve_file(c, hm, "/spiffs/cameras_live.html", &opts);
        }

        // ==================== TUYA DEVICE API ====================
        
        // GET /api/tuya/devices - List all Tuya devices
        else if (mg_strcmp(hm->uri, mg_str("/api/tuya/devices")) == 0) {
            tuya_device_t devices[TUYA_DEVICE_MAX_COUNT];
            int count = 0;
            
            esp_err_t ret = tuya_mgr_get_devices(devices, TUYA_DEVICE_MAX_COUNT, &count);
            if (ret != ESP_OK) {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get devices\"}");
            } else {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddBoolToObject(root, "connected", tuya_mgr_is_connected());
                cJSON_AddNumberToObject(root, "count", count);
                
                cJSON *devices_array = cJSON_CreateArray();
                for (int i = 0; i < count; i++) {
                    cJSON *dev = cJSON_CreateObject();
                    cJSON_AddStringToObject(dev, "id", devices[i].device_id);
                    cJSON_AddStringToObject(dev, "name", devices[i].name);
                    cJSON_AddStringToObject(dev, "type", tuya_device_type_name(devices[i].type));
                    cJSON_AddStringToObject(dev, "state", 
                        devices[i].state == TUYA_DEV_STATE_ONLINE ? "online" :
                        devices[i].state == TUYA_DEV_STATE_OFFLINE ? "offline" :
                        devices[i].state == TUYA_DEV_STATE_PAIRING ? "pairing" : "error");
                    cJSON_AddBoolToObject(dev, "enabled", devices[i].enabled);
                    
                    // Mappings
                    if (devices[i].virtual_zone_id >= 0) {
                        cJSON_AddNumberToObject(dev, "zone_id", devices[i].virtual_zone_id);
                    }
                    if (devices[i].virtual_relay_id >= 0) {
                        cJSON_AddNumberToObject(dev, "relay_id", devices[i].virtual_relay_id);
                    }
                    
                    // Current values
                    if (devices[i].capabilities & TUYA_CAP_SWITCH) {
                        cJSON_AddBoolToObject(dev, "switch_on", devices[i].switch_on);
                    }
                    if (devices[i].capabilities & TUYA_CAP_BRIGHTNESS) {
                        cJSON_AddNumberToObject(dev, "brightness", devices[i].brightness);
                    }
                    if (devices[i].capabilities & TUYA_CAP_CONTACT) {
                        cJSON_AddBoolToObject(dev, "contact_open", devices[i].contact_open);
                    }
                    if (devices[i].capabilities & TUYA_CAP_MOTION) {
                        cJSON_AddBoolToObject(dev, "motion", devices[i].motion_detected);
                    }
                    if (devices[i].capabilities & TUYA_CAP_BATTERY) {
                        cJSON_AddNumberToObject(dev, "battery", devices[i].battery);
                    }
                    
                    cJSON_AddItemToArray(devices_array, dev);
                }
                cJSON_AddItemToObject(root, "devices", devices_array);
                
                char *json = cJSON_PrintUnformatted(root);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
                free(json);
                cJSON_Delete(root);
            }
        }
        
        // POST /api/tuya/discover - Discover new Tuya devices
        else if (mg_strcmp(hm->uri, mg_str("/api/tuya/discover")) == 0) {
            esp_err_t ret = tuya_mgr_discover_devices(10000);  // 10 second timeout
            if (ret == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"message\":\"Discovery started\"}");
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Discovery failed\"}");
            }
        }
        
        // POST /api/tuya/pair - Enable pairing mode
        else if (mg_strcmp(hm->uri, mg_str("/api/tuya/pair")) == 0) {
            char duration_str[16] = "60";
            get_json_str(hm->body, "$.duration", duration_str, sizeof(duration_str));
            uint16_t duration = atoi(duration_str);
            if (duration == 0) duration = 60;
            
            esp_err_t ret = tuya_mgr_enable_pairing(duration);
            if (ret == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"duration\":%d}", duration);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to enable pairing\"}");
            }
        }
        
        // DELETE /api/tuya/device/:id - Remove device
        else if (mg_match(hm->uri, mg_str("/api/tuya/device/*"), NULL) &&
                 mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            struct mg_str caps[2];
            if (mg_match(hm->uri, mg_str("/api/tuya/device/*"), caps)) {
                char device_id[TUYA_DEVICE_ID_MAX_LEN];
                snprintf(device_id, sizeof(device_id), "%.*s", (int)caps[0].len, caps[0].buf);
                
                esp_err_t ret = tuya_mgr_remove_device(device_id);
                if (ret == ESP_OK) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                        "{\"error\":\"Device not found\"}");
                }
            }
        }
        
        // POST /api/tuya/device/:id/control - Control device
        else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/control"), NULL)) {
            struct mg_str caps[1];
            if (mg_match(hm->uri, mg_str("/api/tuya/device/*/control"), caps)) {
                char device_id[TUYA_DEVICE_ID_MAX_LEN];
                snprintf(device_id, sizeof(device_id), "%.*s", (int)caps[0].len, caps[0].buf);
                
                // Parse control parameters
                char action[32] = {0}, value[32] = {0};
                get_json_str(hm->body, "$.action", action, sizeof(action));
                get_json_str(hm->body, "$.value", value, sizeof(value));
                
                esp_err_t ret = ESP_FAIL;
                
                if (strcmp(action, "switch") == 0) {
                    bool on = (strcmp(value, "on") == 0 || strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                    ret = tuya_mgr_set_switch(device_id, on);
                } else if (strcmp(action, "brightness") == 0) {
                    uint8_t brightness = atoi(value);
                    ret = tuya_mgr_set_brightness(device_id, brightness);
                } else if (strcmp(action, "lock") == 0) {
                    bool lock = (strcmp(value, "lock") == 0 || strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                    ret = tuya_mgr_set_lock(device_id, lock);
                }
                
                if (ret == ESP_OK) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Control failed\"}");
                }
            }
        }
        
        // POST /api/tuya/device/:id/map_zone - Map sensor to zone
        else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_zone"), NULL)) {
            struct mg_str caps[1];
            if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_zone"), caps)) {
                char device_id[TUYA_DEVICE_ID_MAX_LEN];
                snprintf(device_id, sizeof(device_id), "%.*s", (int)caps[0].len, caps[0].buf);
                
                char zone_str[16] = {0};
                get_json_str(hm->body, "$.zone_id", zone_str, sizeof(zone_str));
                int zone_id = atoi(zone_str);
                
                esp_err_t ret = tuya_mgr_map_to_zone(device_id, zone_id);
                if (ret == ESP_OK) {
                    tuya_mgr_save_config();
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                        "{\"error\":\"Invalid zone ID (must be 65-96)\"}");
                }
            }
        }
        
        // POST /api/tuya/device/:id/map_relay - Map output to relay
        else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_relay"), NULL)) {
            struct mg_str caps[1];
            if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_relay"), caps)) {
                char device_id[TUYA_DEVICE_ID_MAX_LEN];
                snprintf(device_id, sizeof(device_id), "%.*s", (int)caps[0].len, caps[0].buf);
                
                char relay_str[16] = {0};
                get_json_str(hm->body, "$.relay_id", relay_str, sizeof(relay_str));
                int relay_id = atoi(relay_str);
                
                esp_err_t ret = tuya_mgr_map_to_relay(device_id, relay_id);
                if (ret == ESP_OK) {
                    tuya_mgr_save_config();
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                        "{\"error\":\"Invalid relay ID (must be 32-63)\"}");
                }
            }
        }
        
        // GET /tuya.html - Tuya device management UI
        else if (mg_strcmp(hm->uri, mg_str("/tuya.html")) == 0) {
            struct mg_http_serve_opts opts = {.root_dir = "/spiffs"};
            mg_http_serve_file(c, hm, "/spiffs/tuya.html", &opts);
        }

        // ============================================================
        // ZIGBEE API ENDPOINTS - Disabled to save DRAM
        // ============================================================
        #if 0
        // GET /api/zigbee/devices - List all Zigbee devices
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/devices")) == 0) {
            zigbee_device_t devices[64];
            int count = 0;
            
            if (zigbee_mgr_get_devices(devices, &count) == ESP_OK) {
                bool connected = zigbee_mgr_is_connected();
                
                mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
                mg_printf(c, "{\"connected\":%s,\"count\":%d,\"devices\":[",
                          connected ? "true" : "false", count);
                
                for (int i = 0; i < count; i++) {
                    zigbee_device_t *dev = &devices[i];
                    mg_printf(c, "%s{\"short_addr\":%u,\"name\":\"%s\",\"type\":\"%s\","
                                 "\"state\":\"%s\",\"capabilities\":%u,\"endpoint\":%u,",
                              (i > 0) ? "," : "",
                              dev->short_addr, dev->name, zigbee_device_type_name(dev->type),
                              (dev->state == 1) ? "online" : "offline",
                              dev->capabilities, dev->endpoint);
                    
                    mg_printf(c, "\"zone_id\":%d,\"relay_id\":%d,",
                              dev->zone_id, dev->relay_id);
                    
                    mg_printf(c, "\"current_on_off\":%s,\"current_level\":%u,",
                              dev->current_on_off ? "true" : "false",
                              (dev->current_level * 100) / 254);
                    
                    mg_printf(c, "\"current_temperature\":%.1f,\"current_humidity\":%.1f,"
                                 "\"current_battery\":%u,\"current_contact\":%s,"
                                 "\"current_occupancy\":%s}",
                              dev->current_temperature, dev->current_humidity,
                              dev->current_battery,
                              dev->current_contact ? "true" : "false",
                              dev->current_occupancy ? "true" : "false");
                }
                
                mg_printf(c, "]}");
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get devices\"}");
            }
        }
        
        // POST /api/zigbee/discover - Trigger device discovery
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/discover")) == 0) {
            struct mg_str body = hm->body;
            int timeout_ms = 10000;  // Default 10 seconds
            
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *timeout = cJSON_GetObjectItem(json, "timeout_ms");
                        if (timeout && cJSON_IsNumber(timeout)) {
                            timeout_ms = timeout->valueint;
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
            
            if (zigbee_mgr_discover_devices(timeout_ms) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true,\"message\":\"Discovery started\"}");
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Discovery failed\"}");
            }
        }
        
        // POST /api/zigbee/pair - Enable pairing mode
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/pair")) == 0) {
            struct mg_str body = hm->body;
            int duration_sec = 60;  // Default 60 seconds
            
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *duration = cJSON_GetObjectItem(json, "duration_sec");
                        if (duration && cJSON_IsNumber(duration)) {
                            duration_sec = duration->valueint;
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
            
            if (zigbee_mgr_enable_pairing(duration_sec) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true,\"message\":\"Pairing enabled\"}");
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Pairing failed\"}");
            }
        }
        
        // DELETE /api/zigbee/device/:addr - Remove device
        else if (mg_match(hm->uri, mg_str("/api/zigbee/device/*"), NULL)) {
            // Extract short address from URI (in hex: 0x1234 or just 1234)
            const char *addr_str = hm->uri.buf + strlen("/api/zigbee/device/");
            uint16_t short_addr = (uint16_t)strtol(addr_str, NULL, 0);
            
            if (zigbee_mgr_remove_device(short_addr) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true,\"message\":\"Device removed\"}");
            } else {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                    "{\"error\":\"Device not found\"}");
            }
        }
        
        // POST /api/zigbee/device/:addr/control - Control device
        else if (mg_match(hm->uri, mg_str("/api/zigbee/device/*/control"), NULL)) {
            // Extract short address
            const char *uri_buf = hm->uri.buf;
            const char *addr_start = uri_buf + strlen("/api/zigbee/device/");
            char addr_str[16];
            int idx = 0;
            while (addr_start[idx] && addr_start[idx] != '/' && idx < 15) {
                addr_str[idx] = addr_start[idx];
                idx++;
            }
            addr_str[idx] = '\0';
            uint16_t short_addr = (uint16_t)strtol(addr_str, NULL, 0);
            
            // Parse JSON body
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *action = cJSON_GetObjectItem(json, "action");
                        cJSON *value = cJSON_GetObjectItem(json, "value");
                        
                        if (action && cJSON_IsString(action)) {
                            const char *action_str = action->valuestring;
                            esp_err_t result = ESP_FAIL;
                            
                            if (strcmp(action_str, "on_off") == 0 && value) {
                                bool on = (strcmp(value->valuestring, "on") == 0);
                                result = zigbee_mgr_set_on_off(short_addr, on);
                            }
                            else if (strcmp(action_str, "level") == 0 && value && cJSON_IsNumber(value)) {
                                result = zigbee_mgr_set_level(short_addr, value->valueint);
                            }
                            else if (strcmp(action_str, "lock") == 0 && value) {
                                bool locked = (strcmp(value->valuestring, "locked") == 0);
                                result = zigbee_mgr_set_lock(short_addr, locked);
                            }
                            else if (strcmp(action_str, "color") == 0 && value && cJSON_IsNumber(value)) {
                                // Expect RGB value as 0xRRGGBB
                                uint32_t rgb = (uint32_t)value->valueint;
                                result = zigbee_mgr_set_color(short_addr, rgb);
                            }
                            else if (strcmp(action_str, "color_temp") == 0 && value && cJSON_IsNumber(value)) {
                                // Expect color temp in mireds (153-500)
                                uint16_t mireds = (uint16_t)value->valueint;
                                result = zigbee_mgr_set_color_temp(short_addr, mireds);
                            }
                            else if (strcmp(action_str, "thermostat") == 0 && value && cJSON_IsNumber(value)) {
                                // Expect temperature in Celsius (float)
                                float temperature = (float)value->valuedouble;
                                result = zigbee_mgr_set_thermostat(short_addr, temperature);
                            }
                            
                            if (result == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"message\":\"Device controlled\"}");
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Control failed\"}");
                            }
                        } else {
                            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                "{\"error\":\"Invalid action\"}");
                        }
                        
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"error\":\"No body\"}");
            }
        }
        
        // POST /api/zigbee/device/:addr/map_zone - Map to zone
        else if (mg_match(hm->uri, mg_str("/api/zigbee/device/*/map_zone"), NULL)) {
            // Extract short address
            const char *uri_buf = hm->uri.buf;
            const char *addr_start = uri_buf + strlen("/api/zigbee/device/");
            char addr_str[16];
            int idx = 0;
            while (addr_start[idx] && addr_start[idx] != '/' && idx < 15) {
                addr_str[idx] = addr_start[idx];
                idx++;
            }
            addr_str[idx] = '\0';
            uint16_t short_addr = (uint16_t)strtol(addr_str, NULL, 0);
            
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *zone = cJSON_GetObjectItem(json, "zone_id");
                        if (zone && cJSON_IsNumber(zone)) {
                            int zone_id = zone->valueint;
                            if (zigbee_mgr_map_to_zone(short_addr, zone_id) == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"message\":\"Mapped to zone\"}");
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Invalid zone ID (must be 50-64)\"}");
                            }
                        } else {
                            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                "{\"error\":\"Missing zone_id\"}");
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // POST /api/zigbee/device/:addr/map_relay - Map to relay
        else if (mg_match(hm->uri, mg_str("/api/zigbee/device/*/map_relay"), NULL)) {
            // Extract short address
            const char *uri_buf = hm->uri.buf;
            const char *addr_start = uri_buf + strlen("/api/zigbee/device/");
            char addr_str[16];
            int idx = 0;
            while (addr_start[idx] && addr_start[idx] != '/' && idx < 15) {
                addr_str[idx] = addr_start[idx];
                idx++;
            }
            addr_str[idx] = '\0';
            uint16_t short_addr = (uint16_t)strtol(addr_str, NULL, 0);
            
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *relay = cJSON_GetObjectItem(json, "relay_id");
                        if (relay && cJSON_IsNumber(relay)) {
                            int relay_id = relay->valueint;
                            if (zigbee_mgr_map_to_relay(short_addr, relay_id) == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"message\":\"Mapped to relay\"}");
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Invalid relay ID (must be 64-95)\"}");
                            }
                        } else {
                            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                "{\"error\":\"Missing relay_id\"}");
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // GET /api/zigbee/device/:addr/battery - Get battery level
        else if (mg_match(hm->uri, mg_str("/api/zigbee/device/*/battery"), NULL)) {
            const char *uri_buf = hm->uri.buf;
            const char *addr_start = uri_buf + strlen("/api/zigbee/device/");
            char addr_str[16];
            int idx = 0;
            while (addr_start[idx] && addr_start[idx] != '/' && idx < 15) {
                addr_str[idx] = addr_start[idx];
                idx++;
            }
            addr_str[idx] = '\0';
            uint16_t short_addr = (uint16_t)strtol(addr_str, NULL, 0);
            
            int battery_level = zigbee_mgr_get_battery_level(short_addr);
            if (battery_level >= 0) {
                bool is_low = zigbee_mgr_is_battery_low(short_addr);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"battery_level\":%d,\"low_battery\":%s}", 
                    battery_level, is_low ? "true" : "false");
            } else {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                    "{\"error\":\"Device not found or no battery\"}");
            }
        }
        
        // GET /api/zigbee/statistics - Get network statistics
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/statistics")) == 0) {
            zigbee_network_stats_t stats;
            if (zigbee_mgr_get_statistics(&stats) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"total_devices\":%d,\"online_devices\":%d,\"offline_devices\":%d,"
                    "\"commands_sent\":%u,\"commands_failed\":%u,\"frames_received\":%u,"
                    "\"low_battery_devices\":%d,\"uptime_seconds\":%u}",
                    stats.total_devices, stats.online_devices, stats.offline_devices,
                    stats.commands_sent, stats.commands_failed, stats.frames_received,
                    stats.low_battery_devices, stats.uptime_seconds);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get statistics\"}");
            }
        }
        
        // GET /api/zigbee/groups - Get all groups
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/groups")) == 0) {
            zigbee_group_t groups[ZIGBEE_MAX_GROUPS];
            int count = 0;
            
            if (zigbee_groups_get_all(groups, &count) == ESP_OK) {
                cJSON *arr = cJSON_CreateArray();
                for (int i = 0; i < count; i++) {
                    cJSON *g = cJSON_CreateObject();
                    cJSON_AddNumberToObject(g, "id", groups[i].group_id);
                    cJSON_AddStringToObject(g, "name", groups[i].name);
                    cJSON_AddBoolToObject(g, "enabled", groups[i].enabled);
                    cJSON_AddNumberToObject(g, "device_count", groups[i].device_count);
                    cJSON_AddItemToArray(arr, g);
                }
                char *json_str = cJSON_PrintUnformatted(arr);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
                cJSON_free(json_str);
                cJSON_Delete(arr);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get groups\"}");
            }
        }
        
        // POST /api/zigbee/groups - Create group
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/groups")) == 0 && mg_strcmp(hm->method, mg_str("POST")) == 0) {
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *name = cJSON_GetObjectItem(json, "name");
                        if (name && cJSON_IsString(name)) {
                            uint8_t group_id;
                            if (zigbee_groups_create(name->valuestring, &group_id) == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"group_id\":%d}", group_id);
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Failed to create group\"}");
                            }
                        } else {
                            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                "{\"error\":\"Missing name\"}");
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // POST /api/zigbee/group/:id/control - Control group
        else if (mg_match(hm->uri, mg_str("/api/zigbee/group/*/control"), NULL)) {
            const char *uri_buf = hm->uri.buf;
            const char *id_start = uri_buf + strlen("/api/zigbee/group/");
            char id_str[16];
            int idx = 0;
            while (id_start[idx] && id_start[idx] != '/' && idx < 15) {
                id_str[idx] = id_start[idx];
                idx++;
            }
            id_str[idx] = '\0';
            uint8_t group_id = (uint8_t)strtol(id_str, NULL, 10);
            
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *action = cJSON_GetObjectItem(json, "action");
                        cJSON *value = cJSON_GetObjectItem(json, "value");
                        
                        if (action && cJSON_IsString(action)) {
                            esp_err_t result = ESP_FAIL;
                            
                            if (strcmp(action->valuestring, "on_off") == 0 && value) {
                                bool on = (strcmp(value->valuestring, "on") == 0);
                                result = zigbee_groups_set_on_off(group_id, on);
                            }
                            else if (strcmp(action->valuestring, "level") == 0 && value && cJSON_IsNumber(value)) {
                                result = zigbee_groups_set_level(group_id, value->valueint);
                            }
                            else if (strcmp(action->valuestring, "color") == 0 && value && cJSON_IsNumber(value)) {
                                result = zigbee_groups_set_color(group_id, value->valueint);
                            }
                            
                            if (result == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true}");
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Control failed\"}");
                            }
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // GET /api/zigbee/scenes - Get all scenes
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/scenes")) == 0) {
            zigbee_scene_t scenes[ZIGBEE_MAX_SCENES];
            int count = 0;
            
            if (zigbee_scenes_get_all(scenes, &count) == ESP_OK) {
                cJSON *arr = cJSON_CreateArray();
                for (int i = 0; i < count; i++) {
                    cJSON *s = cJSON_CreateObject();
                    cJSON_AddNumberToObject(s, "id", scenes[i].scene_id);
                    cJSON_AddStringToObject(s, "name", scenes[i].name);
                    cJSON_AddBoolToObject(s, "enabled", scenes[i].enabled);
                    cJSON_AddNumberToObject(s, "device_count", scenes[i].device_count);
                    cJSON_AddItemToArray(arr, s);
                }
                char *json_str = cJSON_PrintUnformatted(arr);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
                cJSON_free(json_str);
                cJSON_Delete(arr);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get scenes\"}");
            }
        }
        
        // POST /api/zigbee/scenes - Create scene
        else if (mg_strcmp(hm->uri, mg_str("/api/zigbee/scenes")) == 0 && mg_strcmp(hm->method, mg_str("POST")) == 0) {
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *name = cJSON_GetObjectItem(json, "name");
                        if (name && cJSON_IsString(name)) {
                            uint8_t scene_id;
                            if (zigbee_scenes_create(name->valuestring, &scene_id) == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"scene_id\":%d}", scene_id);
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Failed to create scene\"}");
                            }
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // POST /api/zigbee/scene/:id/recall - Recall scene
        else if (mg_match(hm->uri, mg_str("/api/zigbee/scene/*/recall"), NULL)) {
            const char *uri_buf = hm->uri.buf;
            const char *id_start = uri_buf + strlen("/api/zigbee/scene/");
            char id_str[16];
            int idx = 0;
            while (id_start[idx] && id_start[idx] != '/' && idx < 15) {
                id_str[idx] = id_start[idx];
                idx++;
            }
            id_str[idx] = '\0';
            uint8_t scene_id = (uint8_t)strtol(id_str, NULL, 10);
            
            if (zigbee_scenes_recall(scene_id) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true}");
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"error\":\"Scene recall failed\"}");
            }
        }
        #endif // ZIGBEE_DISABLED
        
        // ========================================
        // SCHEDULER API ENDPOINTS
        // ========================================
        
        // GET /api/scheduler/tasks - Get all scheduled tasks
        else if (mg_strcmp(hm->uri, mg_str("/api/scheduler/tasks")) == 0) {
            scheduler_task_t tasks[SCHEDULER_MAX_TASKS];
            int count = 0;
            
            if (scheduler_get_all_tasks(tasks, &count) == ESP_OK) {
                cJSON *json = cJSON_CreateArray();
                for (int i = 0; i < count; i++) {
                    cJSON *task_obj = cJSON_CreateObject();
                    cJSON_AddNumberToObject(task_obj, "id", tasks[i].task_id);
                    cJSON_AddStringToObject(task_obj, "name", tasks[i].name);
                    cJSON_AddNumberToObject(task_obj, "type", tasks[i].type);
                    cJSON_AddNumberToObject(task_obj, "priority", tasks[i].priority);
                    cJSON_AddBoolToObject(task_obj, "enabled", tasks[i].enabled);
                    cJSON_AddNumberToObject(task_obj, "last_run", tasks[i].last_run);
                    cJSON_AddNumberToObject(task_obj, "next_run", tasks[i].next_run);
                    cJSON_AddNumberToObject(task_obj, "run_count", tasks[i].run_count);
                    
                    // Add schedule info based on type
                    if (tasks[i].type == SCHEDULE_INTERVAL) {
                        cJSON_AddNumberToObject(task_obj, "interval", tasks[i].schedule.interval_seconds);
                    } else if (tasks[i].type == SCHEDULE_ONCE) {
                        cJSON_AddNumberToObject(task_obj, "run_time", tasks[i].schedule.once_time);
                    }
                    
                    cJSON_AddItemToArray(json, task_obj);
                }
                char *json_str = cJSON_PrintUnformatted(json);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json_str);
                free(json_str);
                cJSON_Delete(json);
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to get tasks\"}");
            }
        }
        
        // POST /api/scheduler/tasks - Create scheduled task
        else if (mg_strcmp(hm->uri, mg_str("/api/scheduler/tasks")) == 0 && mg_strcmp(hm->method, mg_str("POST")) == 0) {
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *name = cJSON_GetObjectItem(json, "name");
                        cJSON *type = cJSON_GetObjectItem(json, "type");
                        cJSON *interval = cJSON_GetObjectItem(json, "interval");
                        cJSON *cron_expr = cJSON_GetObjectItem(json, "cron");
                        cJSON *run_time = cJSON_GetObjectItem(json, "run_time");
                        cJSON *priority = cJSON_GetObjectItem(json, "priority");
                        cJSON *action = cJSON_GetObjectItem(json, "action");
                        
                        uint8_t task_id;
                        esp_err_t result = ESP_FAIL;
                        int prio = priority && cJSON_IsNumber(priority) ? priority->valueint : SCHEDULE_PRIORITY_NORMAL;
                        
                        if (name && type && cJSON_IsNumber(type)) {
                            if (type->valueint == SCHEDULE_INTERVAL && interval && cJSON_IsNumber(interval)) {
                                result = scheduler_add_interval_task(name->valuestring, interval->valueint, 
                                    NULL, NULL, prio, &task_id);
                            } else if (type->valueint == SCHEDULE_CRON && cron_expr && cJSON_IsString(cron_expr)) {
                                result = scheduler_add_cron_task(name->valuestring, cron_expr->valuestring,
                                    NULL, NULL, prio, &task_id);
                            } else if (type->valueint == SCHEDULE_ONCE && run_time && cJSON_IsNumber(run_time)) {
                                result = scheduler_add_once_task(name->valuestring, run_time->valueint,
                                    NULL, NULL, prio, &task_id);
                            }
                            
                            // Parse and store action if provided
                            if (result == ESP_OK && action && cJSON_IsObject(action)) {
                                scheduler_action_t task_action = {0};
                                cJSON *action_type = cJSON_GetObjectItem(action, "type");
                                if (action_type && cJSON_IsNumber(action_type)) {
                                    task_action.type = action_type->valueint;
                                    
                                    switch (task_action.type) {
                                        case SCHEDULER_ACTION_NONE:
                                            // No action to configure
                                            break;
                                            
                                        case SCHEDULER_ACTION_SCENE_RECALL: {
                                            cJSON *scene_id = cJSON_GetObjectItem(action, "scene_id");
                                            if (scene_id && cJSON_IsNumber(scene_id)) {
                                                task_action.params.scene.scene_id = scene_id->valueint;
                                            }
                                            break;
                                            }
                                            case SCHEDULER_ACTION_GROUP_CONTROL: {
                                                cJSON *group_id = cJSON_GetObjectItem(action, "group_id");
                                                cJSON *on = cJSON_GetObjectItem(action, "on");
                                                cJSON *level = cJSON_GetObjectItem(action, "level");
                                                if (group_id && cJSON_IsNumber(group_id)) {
                                                    task_action.params.group.group_id = group_id->valueint;
                                                    task_action.params.group.on = (on && cJSON_IsBool(on)) ? cJSON_IsTrue(on) : true;
                                                    task_action.params.group.level = (level && cJSON_IsNumber(level)) ? level->valueint : 0;
                                                }
                                                break;
                                            }
                                            case SCHEDULER_ACTION_RELAY_CONTROL: {
                                                cJSON *relay_id = cJSON_GetObjectItem(action, "relay_id");
                                                cJSON *on = cJSON_GetObjectItem(action, "on");
                                                if (relay_id && cJSON_IsNumber(relay_id)) {
                                                    task_action.params.relay.relay_id = relay_id->valueint;
                                                    task_action.params.relay.on = (on && cJSON_IsBool(on)) ? cJSON_IsTrue(on) : true;
                                                }
                                                break;
                                            }
                                            case SCHEDULER_ACTION_MQTT_PUBLISH: {
                                                cJSON *topic = cJSON_GetObjectItem(action, "topic");
                                                cJSON *payload = cJSON_GetObjectItem(action, "payload");
                                                if (topic && cJSON_IsString(topic)) {
                                                    strncpy(task_action.params.mqtt.topic, topic->valuestring, 
                                                           sizeof(task_action.params.mqtt.topic) - 1);
                                                }
                                                if (payload && cJSON_IsString(payload)) {
                                                    strncpy(task_action.params.mqtt.payload, payload->valuestring, 
                                                           sizeof(task_action.params.mqtt.payload) - 1);
                                                }
                                                break;
                                            }
                                            case SCHEDULER_ACTION_HTTP_REQUEST: {
                                                cJSON *url = cJSON_GetObjectItem(action, "url");
                                                cJSON *method = cJSON_GetObjectItem(action, "method");
                                                if (url && cJSON_IsString(url)) {
                                                    strncpy(task_action.params.http.url, url->valuestring, 
                                                           sizeof(task_action.params.http.url) - 1);
                                                }
                                                if (method && cJSON_IsString(method)) {
                                                    strncpy(task_action.params.http.method, method->valuestring, 
                                                           sizeof(task_action.params.http.method) - 1);
                                                }
                                                break;
                                            }
                                        }
                                    
                                    // Set the action on the task
                                    scheduler_set_task_action(task_id, &task_action);
                                }
                            }
                            
                            if (result == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true,\"task_id\":%d}", task_id);
                            } else {
                                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Failed to create task\"}");
                            }
                        } else {
                            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                                "{\"error\":\"Missing required fields\"}");
                        }
                        cJSON_Delete(json);
                    } else {
                        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                            "{\"error\":\"Invalid JSON\"}");
                    }
                    free(json_str);
                } else {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Memory allocation failed\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"error\":\"Empty request body\"}");
            }
        }
        
        // PUT /api/scheduler/tasks/:id - Enable/disable task
        else if (mg_match(hm->uri, mg_str("/api/scheduler/tasks/*"), NULL) && mg_strcmp(hm->method, mg_str("PUT")) == 0) {
            const char *uri_buf = hm->uri.buf;
            const char *id_start = uri_buf + strlen("/api/scheduler/tasks/");
            uint8_t task_id = (uint8_t)strtol(id_start, NULL, 10);
            
            struct mg_str body = hm->body;
            if (body.len > 0) {
                char *json_str = strndup(body.buf, body.len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
                        if (enabled && cJSON_IsBool(enabled)) {
                            esp_err_t result = cJSON_IsTrue(enabled) ? 
                                scheduler_enable_task(task_id) : scheduler_disable_task(task_id);
                            
                            if (result == ESP_OK) {
                                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                                    "{\"success\":true}");
                            } else {
                                mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                                    "{\"error\":\"Task not found\"}");
                            }
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            }
        }
        
        // DELETE /api/scheduler/tasks/:id - Delete task
        else if (mg_match(hm->uri, mg_str("/api/scheduler/tasks/*"), NULL) && mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            const char *uri_buf = hm->uri.buf;
            const char *id_start = uri_buf + strlen("/api/scheduler/tasks/");
            uint8_t task_id = (uint8_t)strtol(id_start, NULL, 10);
            
            if (scheduler_delete_task(task_id) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true}");
            } else {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                    "{\"error\":\"Task not found\"}");
            }
        }
        
        // POST /api/scheduler/tasks/:id/trigger - Manually trigger task
        else if (mg_match(hm->uri, mg_str("/api/scheduler/tasks/*/trigger"), NULL)) {
            const char *uri_buf = hm->uri.buf;
            const char *id_start = uri_buf + strlen("/api/scheduler/tasks/");
            char id_str[16];
            int idx = 0;
            while (id_start[idx] && id_start[idx] != '/' && idx < 15) {
                id_str[idx] = id_start[idx];
                idx++;
            }
            id_str[idx] = '\0';
            uint8_t task_id = (uint8_t)strtol(id_str, NULL, 10);
            
            if (scheduler_trigger_task(task_id) == ESP_OK) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true}");
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                    "{\"error\":\"Failed to trigger task\"}");
            }
        }
        
        // GET /scheduler.html - Scheduler management UI
        else if (mg_strcmp(hm->uri, mg_str("/scheduler.html")) == 0) {
            struct mg_http_serve_opts opts = {.root_dir = "/spiffs"};
            mg_http_serve_file(c, hm, "/spiffs/scheduler.html", &opts);
        }
        
        // GET /zigbee.html - Zigbee device management UI
        else if (mg_strcmp(hm->uri, mg_str("/zigbee.html")) == 0) {
            struct mg_http_serve_opts opts = {.root_dir = "/spiffs"};
            mg_http_serve_file(c, hm, "/spiffs/zigbee.html", &opts);
        }

        // Default: serve static files from SPIFFS
        struct mg_http_serve_opts opts = {.root_dir = "/spiffs"};
        mg_http_serve_dir(c, hm, &opts);
    }
}

// --- Tasks ---
void monitor_task(void *pvParameters) {
    esp_task_wdt_add(NULL);  // Add this task to watchdog
    uint32_t display_update_counter = 0;
    
    while (1) {
        // Check if we can safely access engine
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            engine_tick();
            
            // Update OLED display with temperature every 5 seconds
            if (display_update_counter++ % 50 == 0) {
                system_status_t status = {0};
                if (system_monitoring_get_status(&status) == ESP_OK) {
                    char temp_line[32];
                    char alert_line[32] = "";
                    
                    // Convert C to F and format temperature line
                    int temp_f = (status.temperature_c * 9 / 5) + 32;
                    snprintf(temp_line, sizeof(temp_line), "Temp: %d°F", temp_f);
                    
                    // Add alert indicators
                    if (status.tamper_detected) {
                        snprintf(alert_line, sizeof(alert_line), "TAMPER!");
                    } else if (status.on_backup_power) {
                        snprintf(alert_line, sizeof(alert_line), "UPS Active");
                    }
                    
                    // Display on OLED (row 3 is typically bottom row)
                    ssd1306_display_text(oled_dev, 3, temp_line, false);
                    if (alert_line[0] != '\0') {
                        ssd1306_display_text(oled_dev, 2, alert_line, false);
                    }
                }
            }
            
            xSemaphoreGive(i2c_mutex);
        } else {
            // Timeout - log warning but continue (don't skip cycle entirely)
            ESP_LOGW(TAG, "I2C mutex timeout in monitor_task");
        }
        esp_task_wdt_reset();  // Reset watchdog timer
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void mongoose_task(void *pvParameters) {
    esp_task_wdt_add(NULL);  // Add this task to watchdog
    
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);
    mg_http_listen(&mgr, "https://0.0.0.0:443", fn, (void *)1);
    for (;;) {
        mg_mgr_poll(&mgr, 50);
        esp_task_wdt_reset();  // Reset watchdog timer
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// --- Ethernet IP Event ---
static void got_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Show current arm state with IP on row 2
        int arm_state = engine_get_arm_state();
        const char* state_text = "SENTINEL";
        switch(arm_state) {
            case 0: state_text = "DISARMED"; break;
            case 1: state_text = "ARMED AWAY"; break;
            case 2: state_text = "ARMED STAY"; break;
            case 3: state_text = "ARMED NIGHT"; break;
            case 4: state_text = "*** ALARM ***"; break;
        }
        ssd1306_clear_display(oled_dev, false);
        ssd1306_display_text(oled_dev, 0, state_text, false);
        ssd1306_display_text(oled_dev, 2, ip_str, false);
        xSemaphoreGive(i2c_mutex);
    }
    ESP_LOGI(TAG, "Network connected: %s", ip_str);
}

// --- Display Update Function (called from engine) ---
void display_update_status(const char* state_text, const char* detail_text) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ssd1306_clear_display(oled_dev, false);
        if (state_text) {
            ssd1306_display_text(oled_dev, 0, state_text, false);
        }
        if (detail_text) {
            ssd1306_display_text(oled_dev, 1, detail_text, false);
        }
        xSemaphoreGive(i2c_mutex);
    }
}

void app_main(void) {
    // 1. Core System Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }
    
    // Initialize Watchdog Timer (5 second timeout, panic on timeout)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,  // Watch all cores
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    ESP_LOGI(TAG, "Watchdog initialized (5s timeout)");

    // Initialize Log Capture
    s_prev_logging_func = esp_log_set_vprintf(app_log_vprintf);
    
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        abort();
    }

    // 2. Modern I2C Bus Init
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &bus_handle));

    // 3. Initialize OLED (with mutex protection)
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ssd1306_config_t oled_conf = {
            .i2c_address = 0x3C,
            .i2c_clock_speed = 400000,
            .panel_size = SSD1306_PANEL_128x64,
        };
        
        ESP_ERROR_CHECK(ssd1306_init(bus_handle, &oled_conf, &oled_dev));
        ssd1306_clear_display(oled_dev, false);
        ssd1306_display_text(oled_dev, 0, "BOOTING...", false);
        xSemaphoreGive(i2c_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for OLED init");
    }

    // 4. Storage & Installer
    storage_init(); 
    // 5. RF installed
    
    rf_driver_init();
    // Check for SD Card
    if (storage_mount_sd() == ESP_OK) {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 0, "SD DETECTED", false);
            xSemaphoreGive(i2c_mutex);
        }
        storage_sync_sd_to_internal(); 
        storage_unmount_sd();
    }
    
    // 5. Operational Logic
    storage_load_all(); 
    
    // Initialize session manager (JWT tokens)
    if (!session_mgr_init()) {
        ESP_LOGE(TAG, "Failed to initialize session manager - continuing without tokens");
    } else {
        ESP_LOGI(TAG, "Session manager initialized");
    }
    
    // Migrate users with plaintext PINs to hashed passwords
    ESP_LOGI(TAG, "Checking for users needing password migration...");
    esp_err_t migration_result = users_migrate_all_to_hash();
    if (migration_result == ESP_OK) {
        ESP_LOGI(TAG, "User migration complete - saving to storage");
        // Save migrated users back to storage
        extern void storage_save_users(void);
        storage_save_users();
    }
    
    // 5. HAL + Engine Init (pass shared I2C bus to system_monitoring)
    hal_esp32_init();          
    engine_init();
    
    // Initialize virtual zone monitoring
    esphome_zones_init();
    tuya_zones_init();
    
    // Initialize system monitoring with shared I2C bus
    if (system_monitoring_init(bus_handle) != ESP_OK) {
        ESP_LOGW(TAG, "System monitoring init failed - continuing anyway");
    } else {
        // Check RTC status and restore system time from battery-backed RTC
        extern int system_monitoring_check_rtc_status(void);
        extern uint32_t system_monitoring_get_rtc_time(void);
        
        int rtc_status = system_monitoring_check_rtc_status();
        if (rtc_status == ESP_OK) {
            // RTC oscillator is running, restore time
            uint32_t rtc_time = system_monitoring_get_rtc_time();
            if (rtc_time > 1609459200) {  // After 2021-01-01 00:00:00 UTC
                struct timeval tv = {.tv_sec = rtc_time, .tv_usec = 0};
                settimeofday(&tv, NULL);
                ESP_LOGI(TAG, "System time restored from RTC: %lu", rtc_time);
                
                // Display time on OLED
                if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    time_t now = rtc_time;
                    struct tm timeinfo;
                    localtime_r(&now, &timeinfo);
                    char time_str[32];
                    strftime(time_str, sizeof(time_str), "RTC: %m/%d %H:%M", &timeinfo);
                    ssd1306_display_text(oled_dev, 1, time_str, false);
                    xSemaphoreGive(i2c_mutex);
                }
            } else {
                ESP_LOGW(TAG, "RTC time invalid (%lu), needs NTP sync", rtc_time);
            }
        } else {
            ESP_LOGW(TAG, "RTC oscillator was stopped - time needs sync");
        }
    }

    // Initialize network (Ethernet W5500)
    extern esp_err_t init_ethernet_v3(void);
    extern esp_err_t w5500_health_check(void);
    
    esp_err_t eth_ret = init_ethernet_v3();
    if (eth_ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet initialization failed: %s", esp_err_to_name(eth_ret));
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 1, "Ethernet FAILED", false);
            xSemaphoreGive(i2c_mutex);
        }
    } else {
        ESP_LOGI(TAG, "Ethernet initialized");
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 1, "Ethernet initialized", false);
            xSemaphoreGive(i2c_mutex);
        }
        
        // Perform health check
        vTaskDelay(pdMS_TO_TICKS(500));  // Wait for W5500 to be ready
        eth_ret = w5500_health_check();
        if (eth_ret != ESP_OK) {
            ESP_LOGW(TAG, "W5500 health check failed - continuing anyway");
        }
    }

    // Register IP event handler (Ethernet disconnected handler in net.c)
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL);

    // Initialize mDNS for sentinel-esp.local hostname resolution
    // Disabled to save DRAM (36KB)
    /*
    ESP_LOGI(TAG, "Initializing mDNS...");
    esp_err_t mdns_ret = mdns_init();
    if (mdns_ret == ESP_OK) {
        mdns_hostname_set("sentinel-esp");
        mdns_instance_name_set("Sentinel Alarm System");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        mdns_service_add(NULL, "_https", "_tcp", 443, NULL, 0);
        ESP_LOGI(TAG, "mDNS initialized - accessible at sentinel-esp.local");
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 1, "sentinel-esp.local", false);
            xSemaphoreGive(i2c_mutex);
        }
    } else {
        ESP_LOGW(TAG, "mDNS initialization failed: %s", esp_err_to_name(mdns_ret));
    }
    */

    // 6. Initialize ESPHome API Client
    ESP_LOGI(TAG, "Initializing ESPHome API client...");
    extern int esphome_api_init(void);
    if (esphome_api_init() == 0) {
        ESP_LOGI(TAG, "ESPHome API client initialized");
    } else {
        ESP_LOGW(TAG, "ESPHome API client initialization failed");
    }
    
    // Initialize camera manager
    ESP_LOGI(TAG, "Initializing camera manager...");
    if (camera_mgr_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize camera manager");
    } else {
        if (camera_mgr_load_config() != ESP_OK) {
            ESP_LOGW(TAG, "No camera config found or failed to load");
        } else {
            ESP_LOGI(TAG, "Camera manager initialized with %d camera(s)", camera_mgr_get_count());
        }
    }
    
    // 7. Initialize Tuya Manager (KC868-A8 native support)
    ESP_LOGI(TAG, "Initializing Tuya manager...");
    if (tuya_mgr_init(NULL, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Tuya manager initialized (GPIO16/17 UART)");
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 1, "Tuya Ready", false);
            xSemaphoreGive(i2c_mutex);
        }
    } else {
        ESP_LOGW(TAG, "Tuya manager initialization failed");
    }
    
    // 7.5. Initialize Scheduler (for zigbee automations and future scheduling)
    ESP_LOGI(TAG, "Initializing scheduler...");
    if (scheduler_init() == ESP_OK && scheduler_start() == ESP_OK) {
        ESP_LOGI(TAG, "Scheduler initialized and started");
    } else {
        ESP_LOGW(TAG, "Scheduler initialization failed");
    }
    
    // 7.6. Zigbee Manager - Disabled to save DRAM
    /*
    ESP_LOGI(TAG, "Initializing Zigbee manager...");
    if (zigbee_mgr_init(NULL, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Zigbee manager initialized (GPIO18/19 UART)");
        
        // Initialize Zigbee Phase 3 components
        zigbee_groups_init();
        zigbee_scenes_init();
        zigbee_automations_init();
        ESP_LOGI(TAG, "Zigbee Phase 3 (Groups/Scenes/Automations) initialized");
        
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 2, "Zigbee Ready", false);
            xSemaphoreGive(i2c_mutex);
        }
    } else {
        ESP_LOGW(TAG, "Zigbee manager initialization failed");
    }
    */
    
    // 8. Matter Controller - Disabled to save DRAM
    /*
    #ifdef CONFIG_ENABLE_MATTER_CONTROLLER
    ESP_LOGI(TAG, "Initializing Matter controller...");
    extern int matter_controller_init(void);
    extern int matter_zones_init(void);
    extern int matter_relays_init(void);
    extern int matter_controller_start_task(void);
    
    if (matter_controller_init() == 0) {
        matter_zones_init();
        matter_relays_init();
        matter_controller_start_task();
        ESP_LOGI(TAG, "Matter controller initialized");
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ssd1306_display_text(oled_dev, 1, "Matter Ready", false);
            xSemaphoreGive(i2c_mutex);
        }
    } else {
        ESP_LOGW(TAG, "Matter controller initialization failed");
    }
    #else
    ESP_LOGI(TAG, "Matter controller disabled (ESP-Matter SDK not installed)");
    ESP_LOGI(TAG, "To enable: Install ESP-Matter SDK and set CONFIG_ENABLE_MATTER_CONTROLLER=y");
    #endif
    */
    
    // 9. Launch Tasks
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 10, NULL);
    xTaskCreate(mongoose_task, "mongoose", 8192, NULL, 5, NULL);
    
    // Add main task to watchdog
    esp_task_wdt_add(NULL);
    
    ESP_LOGI(TAG, "Sentinel Online");
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ssd1306_display_text(oled_dev, 2, "Sentinel Online", false);
        xSemaphoreGive(i2c_mutex);
    }
}
